#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <commdlg.h>
#include "aes/aes_form.h"
#include "aes/aes_object.h"
#include "aes/aes_window.h"
#include "screen.h"
#include "screen_canvas.h"
#include "guest_mem_util.h"
#include "m68k.h"
#include "logger.h"
#include "tos_layer.h"

// form_dial() flags
#define FMD_START  0
#define FMD_GROW   1
#define FMD_SHRINK 2
#define FMD_FINISH 3

// FORM_DO

static void redraw_object(unsigned int tree, int16_t root_ax, int16_t root_ay, int nobs, int obj_idx) {
    int16_t oax, oay;
    if (!aes_object_abs_pos(tree, tree, root_ax, root_ay, nobs, obj_idx, &oax, &oay)) {
        return;
    }
    unsigned int obj_addr = aes_object_addr(tree, obj_idx);
    int16_t rect[4];
    aes_object_rect(obj_addr, oax, oay, rect);
    aes_objc_draw_at(tree, obj_addr, rect[0], rect[1], rect[0], rect[1], rect[2], rect[3], 1);
}

// [FULL] Runs the dialog to completion before returning, which is what makes it modal from the guest's point of view.
void aes_form_do(const AesPB *pb) {
    unsigned int tree = aes_pb_addrin(pb, 0);
    int16_t start_edit = aes_pb_intin(pb, 0);

    int nobs = aes_object_tree_limit(tree);

    int16_t root_ax = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_X);
    int16_t root_ay = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_Y);
    int16_t root_w  = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_W);
    int16_t root_h  = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_H);

    aes_window_begin_overlay();

    aes_objc_draw_at(tree, tree, root_ax, root_ay, root_ax, root_ay, root_w, root_h, 8);

    int default_obj = aes_object_find_flagged(tree, tree, nobs, OF_DEFAULT);

    int focus_obj = (start_edit > 0) ? start_edit : -1;
    int cursor_pos = 0;
    if (focus_obj >= 0) {
        unsigned int fobj_addr = aes_object_addr(tree, focus_obj);
        unsigned int spec = m68k_read_memory_32(fobj_addr + OBJECT_OFS_SPEC);
        unsigned int ptext = m68k_read_memory_32(spec + TE_OFS_PTEXT);
        char buf[256];
        guest_read_cstring(ptext, buf, sizeof(buf));
        cursor_pos = (int)strlen(buf);
    }

    int pressed_obj = -1;
    int exit_obj = -1;
    int double_click = 0;
    DWORD last_click_time = 0;
    int last_click_obj = -1;
    int prev_down = 0;

    for (;;) {
        canvas_pump_messages();

        if (tos_is_halted()) {
            aes_window_end_overlay();
            return;
        }

        int down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;
        if (down != prev_down) {
            prev_down = down;
            int gx, gy;
            aes_window_cursor_pos(&gx, &gy);
            int16_t mx = (int16_t)gx;
            int16_t my = (int16_t)gy;

            if (down) {
                int parent_head = -1, parent_tail = -1;
                int hit = aes_object_find_at(tree, tree, root_ax, root_ay, mx, my, nobs, 8, &parent_head, &parent_tail);
                pressed_obj = hit;

                if (hit >= 0) {
                    unsigned int hit_addr = aes_object_addr(tree, hit);
                    unsigned int hflags = m68k_read_memory_16(hit_addr + OBJECT_OFS_FLAGS);

                    if (hflags & OF_TOUCHEXIT) {
                        exit_obj = hit;
                    } else if (hflags & OF_EDITABLE) {
                        focus_obj = hit;
                        unsigned int spec = m68k_read_memory_32(hit_addr + OBJECT_OFS_SPEC);
                        unsigned int ptext = m68k_read_memory_32(spec + TE_OFS_PTEXT);
                        char buf[256];
                        guest_read_cstring(ptext, buf, sizeof(buf));
                        cursor_pos = (int)strlen(buf);
                    } else if (hflags & OF_SELECTABLE) {
                        unsigned int hstate = m68k_read_memory_16(hit_addr + OBJECT_OFS_STATE);
                        if ((hflags & OF_RBUTTON) && parent_head >= 0) {
                            // Only one radio button in a group may be set, so clear whichever sibling holds the selection.
                            for (AesChildIter sib = aes_object_child_run(tree, (unsigned int)parent_head, (unsigned int)parent_tail, nobs); aes_object_child_valid(&sib); aes_object_child_next(&sib)) {
                                unsigned int sflags = m68k_read_memory_16(sib.addr + OBJECT_OFS_FLAGS);
                                if (!(sflags & OF_RBUTTON) || sib.index == (unsigned int)hit) {
                                    continue;
                                }
                                unsigned int sstate = m68k_read_memory_16(sib.addr + OBJECT_OFS_STATE);
                                if (sstate & OS_SELECTED) {
                                    m68k_write_memory_16(sib.addr + OBJECT_OFS_STATE, sstate & ~OS_SELECTED);
                                    redraw_object(tree, root_ax, root_ay, nobs, (int)sib.index);
                                }
                            }
                            hstate |= OS_SELECTED;
                        } else {
                            hstate ^= OS_SELECTED;
                        }
                        m68k_write_memory_16(hit_addr + OBJECT_OFS_STATE, hstate);
                        redraw_object(tree, root_ax, root_ay, nobs, hit);
                    }
                }
            } else if (pressed_obj >= 0) {
                unsigned int rel_addr = aes_object_addr(tree, pressed_obj);
                unsigned int rflags = m68k_read_memory_16(rel_addr + OBJECT_OFS_FLAGS);
                int16_t oax, oay;
                if ((rflags & OF_EXIT) && aes_object_abs_pos(tree, tree, root_ax, root_ay, nobs, pressed_obj, &oax, &oay)) {
                    int16_t rrect[4];
                    aes_object_rect(rel_addr, oax, oay, rrect);
                    if (mx >= rrect[0] && mx < rrect[0] + rrect[2] && my >= rrect[1] && my < rrect[1] + rrect[3]) {
                        DWORD now = GetTickCount();
                        double_click = (last_click_obj == pressed_obj && (now - last_click_time) < GetDoubleClickTime());
                        last_click_obj = pressed_obj;
                        last_click_time = now;
                        exit_obj = pressed_obj;
                    }
                }
                pressed_obj = -1;
            }
        }

        if (exit_obj < 0 && focus_obj >= 0) {
            unsigned int key;
            while (exit_obj < 0 && aes_window_poll_key(&key)) {
                unsigned int ascii = key & 0xFF;
                unsigned int scan = (key >> 8) & 0xFF;
                unsigned int fobj_addr = aes_object_addr(tree, focus_obj);
                unsigned int spec = m68k_read_memory_32(fobj_addr + OBJECT_OFS_SPEC);
                unsigned int ptext = m68k_read_memory_32(spec + TE_OFS_PTEXT);
                int16_t tmplen = (int16_t)m68k_read_memory_16(spec + TE_OFS_TMPLEN);
                char buf[256];
                guest_read_cstring(ptext, buf, sizeof(buf));
                int len = (int)strlen(buf);
                int max_len = (tmplen > 0 && tmplen < (int16_t)sizeof(buf)) ? tmplen : (int)sizeof(buf) - 1;

                if (ascii == 0x0D) {
                    if (default_obj >= 0) {
                        exit_obj = default_obj;
                    }
                    continue;
                } else if (ascii == 0x08) {
                    if (cursor_pos > 0 && cursor_pos <= len) {
                        memmove(&buf[cursor_pos - 1], &buf[cursor_pos], (size_t)(len - cursor_pos + 1));
                        cursor_pos--;
                        guest_write_cstring(ptext, buf, (size_t)(max_len + 1));
                    }
                } else if (scan == AES_SCAN_DELETE) {
                    if (cursor_pos < len) {
                        memmove(&buf[cursor_pos], &buf[cursor_pos + 1], (size_t)(len - cursor_pos));
                        guest_write_cstring(ptext, buf, (size_t)(max_len + 1));
                    }
                } else if (scan == AES_SCAN_LEFT) {
                    if (cursor_pos > 0) cursor_pos--;
                } else if (scan == AES_SCAN_RIGHT) {
                    if (cursor_pos < len) cursor_pos++;
                } else if (ascii >= 0x20 && ascii < 0x7F) {
                    if (len < max_len) {
                        memmove(&buf[cursor_pos + 1], &buf[cursor_pos], (size_t)(len - cursor_pos + 1));
                        buf[cursor_pos] = (char)ascii;
                        cursor_pos++;
                        guest_write_cstring(ptext, buf, (size_t)(max_len + 1));
                    }
                }

                redraw_object(tree, root_ax, root_ay, nobs, focus_obj);
            }
        }

        if (exit_obj >= 0) {
            break;
        }
        Sleep(10);
    }

    if (aes_window_end_overlay() == 0) {
        aes_window_overlay_finished(root_ax, root_ay, root_w, root_h);
    }

    int16_t result = (int16_t)((exit_obj & 0x7FFF) | (double_click ? 0x8000 : 0));
    aes_pb_set_intout(pb, 0, result);

    log_write(LOG_API, "form_do(tree=0x%08X, start_edit=%d) -> exit object %d%s", tree, start_edit, exit_obj, double_click ? " (double-click)" : "");
}

// FORM DIALOGS

static const char *form_dial_name(int16_t flag) {
    switch (flag) {
        case FMD_START:  return "FMD_START";
        case FMD_GROW:   return "FMD_GROW";
        case FMD_SHRINK: return "FMD_SHRINK";
        case FMD_FINISH: return "FMD_FINISH";
        default:         return "FMD_?";
    }
}

static void *s_dialog_snapshot = NULL;

// [PARTIAL] FMD_START and FMD_FINISH reserve and restore the area; FMD_GROW and FMD_SHRINK are accepted without drawing the zoom animation.
void aes_form_dial(const AesPB *pb) {
    int16_t flag = aes_pb_intin(pb, 0);
    int16_t big_x = aes_pb_intin(pb, 5);
    int16_t big_y = aes_pb_intin(pb, 6);
    int16_t big_w = aes_pb_intin(pb, 7);
    int16_t big_h = aes_pb_intin(pb, 8);

    aes_pb_set_intout(pb, 0, 1);
    aes_pb_set_intout(pb, 1, big_x);
    aes_pb_set_intout(pb, 2, big_y);
    aes_pb_set_intout(pb, 3, big_w);
    aes_pb_set_intout(pb, 4, big_h);

    int restored_from_snapshot = 0;
    if (flag == FMD_START) {
        aes_window_begin_overlay();
        s_dialog_snapshot = canvas_save_area(big_x, big_y, big_w, big_h);
    } else if (flag == FMD_FINISH) {
        aes_window_end_overlay();
        if (s_dialog_snapshot) {
            canvas_restore_area(s_dialog_snapshot);
            s_dialog_snapshot = NULL;
            restored_from_snapshot = 1;
        } else {
            aes_window_overlay_finished(big_x, big_y, big_w, big_h);
        }
    }

    log_write(LOG_API, "form_dial(%s, %d,%d,%d,%d) -> %s", form_dial_name(flag), big_x, big_y, big_w, big_h, flag == FMD_START ? (s_dialog_snapshot ? "screen reserved, snapshot saved" : "screen reserved, no snapshot (falls back to app redraw on finish)") : flag == FMD_FINISH ? (restored_from_snapshot ? "screen released, restored from snapshot" : "screen released, covered area restored") : "accepted, no zoom animation");
}

// [FULL] Moves the root object itself, since every child position is relative to it.
void aes_form_center(const AesPB *pb) {
    unsigned int tree = aes_pb_addrin(pb, 0);

    int16_t w = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_W);
    int16_t h = (int16_t)m68k_read_memory_16(tree + OBJECT_OFS_H);

    int screen_w = GUEST_SCREEN_W;
    int screen_h = GUEST_SCREEN_H;

    int16_t x = (int16_t)((screen_w - w) / 2);
    int16_t y = (int16_t)((screen_h - h) / 2);
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    m68k_write_memory_16(tree + OBJECT_OFS_X, (unsigned int)(uint16_t)x);
    m68k_write_memory_16(tree + OBJECT_OFS_Y, (unsigned int)(uint16_t)y);

    aes_pb_set_intout(pb, 0, 1);
    aes_pb_set_intout(pb, 1, x);
    aes_pb_set_intout(pb, 2, y);
    aes_pb_set_intout(pb, 3, w);
    aes_pb_set_intout(pb, 4, h);

    log_write(LOG_API, "form_center(tree=0x%08X) -> w=%d,h=%d, root object moved to %d,%d on %dx%d screen", tree, w, h, x, y, screen_w, screen_h);
}

// FORM ALERT & FILE SELECTOR

static const char *alert_take_bracket(const char *p, char *out, size_t max_len) {
    if (*p != '[') {
        out[0] = '\0';
        return p;
    }
    p++;
    size_t i = 0;
    while (*p && *p != ']' && i < max_len - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    if (*p == ']') {
        p++;
    }
    return p;
}

// [FULL] The alert string packs icon, message and buttons into three bracketed fields, which is why it is parsed rather than simply displayed.
void aes_form_alert(const AesPB *pb) {
    int16_t default_button = aes_pb_intin(pb, 0);
    unsigned int str_addr = aes_pb_addrin(pb, 0);

    char raw[512];
    guest_read_cstring(str_addr, raw, sizeof(raw));

    char icon_buf[8], msg_buf[400], btn_buf[128];
    const char *p = raw;
    p = alert_take_bracket(p, icon_buf, sizeof(icon_buf));
    p = alert_take_bracket(p, msg_buf, sizeof(msg_buf));
    alert_take_bracket(p, btn_buf, sizeof(btn_buf));

    int icon = atoi(icon_buf);

    char message[400];
    size_t mi = 0;
    for (const char *q = msg_buf; *q && mi < sizeof(message) - 1; q++) {
        message[mi++] = (*q == '|') ? '\n' : *q;
    }
    message[mi] = '\0';

    char btn_copy[128];
    strncpy(btn_copy, btn_buf, sizeof(btn_copy) - 1);
    btn_copy[sizeof(btn_copy) - 1] = '\0';

    const char *labels[8];
    int nbuttons = 0;
    for (char *tok = strtok(btn_copy, "|"); tok && nbuttons < 8; tok = strtok(NULL, "|")) {
        labels[nbuttons++] = tok;
    }

    if (nbuttons > 1) {
        for (int i = 0; i < nbuttons; i++) {
            size_t mlen = strlen(message);
            if (mlen + 8 >= sizeof(message)) {
                break;
            }
            _snprintf(message + mlen, sizeof(message) - mlen - 1, "%s%d: %s", i ? "\n" : "\n\n", i + 1, labels[i]);
            message[sizeof(message) - 1] = '\0';
        }
    }

    UINT icon_flag = MB_ICONINFORMATION;
    if (icon == 2) {
        icon_flag = MB_ICONWARNING;
    } else if (icon == 3) {
        icon_flag = MB_ICONERROR;
    }

    UINT type_flag = MB_OK;
    if (nbuttons == 2) {
        type_flag = MB_OKCANCEL;
    } else if (nbuttons >= 3) {
        type_flag = MB_YESNOCANCEL;
    }

    UINT def_flag = MB_DEFBUTTON1;
    if (default_button == 2) {
        def_flag = MB_DEFBUTTON2;
    } else if (default_button >= 3) {
        def_flag = MB_DEFBUTTON3;
    }

    int result = MessageBoxA(NULL, message, "GEM Alert", type_flag | icon_flag | def_flag);

    int clicked = 1;
    if (nbuttons == 2) {
        clicked = (result == IDOK) ? 1 : 2;
    } else if (nbuttons >= 3) {
        if (result == IDYES) clicked = 1;
        else if (result == IDNO) clicked = 2;
        else clicked = 3;
    }

    aes_pb_set_intout(pb, 0, (int16_t)clicked);

    log_write(LOG_API, "form_alert(default=%d, icon=%d, \"%s\") -> %d button(s), clicked=%d", default_button, icon, message, nbuttons, clicked);
}

// [PARTIAL] Delegates to the host's own file dialog rather than drawing GEM's selector, so the result has to be mapped back under C:\ and only selections beneath the working directory can be expressed that way.
void aes_fsel_input(const AesPB *pb) {
    unsigned int path_addr = aes_pb_addrin(pb, 0);
    unsigned int sel_addr = aes_pb_addrin(pb, 1);

    char path_and_mask[MAX_PATH];
    guest_read_cstring(path_addr, path_and_mask, sizeof(path_and_mask));
    char initial_sel[MAX_PATH];
    guest_read_cstring(sel_addr, initial_sel, sizeof(initial_sel));

    char mask[64] = "*.*";
    const char *last_slash = strrchr(path_and_mask, '\\');
    if (last_slash && last_slash[1] != '\0') {
        strncpy(mask, last_slash + 1, sizeof(mask) - 1);
        mask[sizeof(mask) - 1] = '\0';
    }

    char host_cwd[MAX_PATH];
    DWORD cwd_len = GetCurrentDirectoryA(sizeof(host_cwd), host_cwd);
    const char *init_dir = (cwd_len > 0 && cwd_len < sizeof(host_cwd)) ? host_cwd : NULL;

    char filter[128];
    _snprintf(filter, sizeof(filter), "Files (%s)%c%s%c%c", mask, '\0', mask, '\0', '\0');

    char file_buf[MAX_PATH];
    strncpy(file_buf, initial_sel, sizeof(file_buf) - 1);
    file_buf[sizeof(file_buf) - 1] = '\0';

    OPENFILENAMEA ofn;
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = NULL;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file_buf;
    ofn.nMaxFile = sizeof(file_buf);
    ofn.lpstrInitialDir = init_dir;
    ofn.lpstrTitle = "GEM File Selector";
    ofn.Flags = OFN_NOCHANGEDIR | OFN_HIDEREADONLY;

    BOOL ok = GetOpenFileNameA(&ofn);

    int button = ok ? 1 : 0;
    if (ok) {
        const char *real_sep = strrchr(file_buf, '\\');
        const char *out_name = real_sep ? real_sep + 1 : file_buf;

        char out_dir[MAX_PATH] = "C:\\";
        if (real_sep && init_dir) {
            size_t cwd_len_sz = (size_t)cwd_len;
            int under_cwd = 1;
            for (size_t i = 0; i < cwd_len_sz; i++) {
                if (!file_buf[i] || tolower((unsigned char)file_buf[i]) != tolower((unsigned char)init_dir[i])) {
                    under_cwd = 0;
                    break;
                }
            }
            if (under_cwd) {
                const char *rel = file_buf + cwd_len_sz;
                while (*rel == '\\') rel++;
                size_t rel_dir_len = (real_sep >= rel) ? (size_t)(real_sep - rel) : 0;
                if (rel_dir_len > 0) {
                    _snprintf(out_dir, sizeof(out_dir), "C:\\%.*s\\", (int)rel_dir_len, rel);
                }
            }
        }

        guest_write_cstring(path_addr, out_dir, MAX_PATH);
        guest_write_cstring(sel_addr, out_name, MAX_PATH);
    }

    aes_pb_set_intout(pb, 0, 1);
    aes_pb_set_intout(pb, 1, (int16_t)button);

    log_write(LOG_API, "fsel_input(path=\"%s\") -> %s%s%s", path_and_mask, ok ? "OK, selected \"" : "Cancel", ok ? file_buf : "", ok ? "\"" : "");
}