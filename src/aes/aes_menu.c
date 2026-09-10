#include <windows.h>
#include "aes/aes_menu.h"
#include "aes/aes_object.h"
#include "aes/aes_rsrc.h"
#include "aes/aes_window.h"
#include "vdi/vdi_attr.h"
#include "guest_mem_util.h"
#include "m68k.h"
#include "logger.h"
#include "screen.h"
#include "tos_layer.h"

#define MAX_TITLES 32
#define DEFAULT_BAR_H 19

static unsigned int s_menu_tree = 0;
static int16_t s_bar_h = 0;

// OBJECT ACCESS

static unsigned int obj_at(int idx) {
    return aes_object_addr(s_menu_tree, idx);
}

static int16_t obj_word(int idx, int ofs) {
    return (int16_t)m68k_read_memory_16(obj_at(idx) + ofs);
}

static unsigned int obj_state(int idx) {
    return m68k_read_memory_16(obj_at(idx) + OBJECT_OFS_STATE);
}

static void obj_set_state(int idx, unsigned int state) {
    m68k_write_memory_16(obj_at(idx) + OBJECT_OFS_STATE, state);
}

static int collect_children(int parent, int out[], int max) {
    int nobs = aes_object_tree_limit(s_menu_tree);
    int n = 0;
    for (AesChildIter it = aes_object_child_first(s_menu_tree, obj_at(parent), nobs); aes_object_child_valid(&it) && n < max; aes_object_child_next(&it)) {
        out[n++] = (int)it.index;
    }
    return n;
}

static int menu_parts(int *root, int *titles_parent, int *menus_parent) {
    if (s_menu_tree == 0 || aes_rsrc_object_base() == 0) {
        return 0;
    }

    int nobs = aes_object_tree_limit(s_menu_tree);
    int r = 0; // the tree's root is index 0 within it, by definition

    int bar = obj_word(r, OBJECT_OFS_HEAD);
    int menus = obj_word(r, OBJECT_OFS_TAIL);
    if (bar < 0 || bar >= nobs || menus < 0 || menus >= nobs || bar == menus) {
        return 0;
    }

    int titles = obj_word(bar, OBJECT_OFS_HEAD);
    if (titles < 0 || titles >= nobs) {
        return 0;
    }

    *root = r;
    *titles_parent = titles;
    *menus_parent = menus;
    return 1;
}

// GEOMETRY

typedef struct {
    int count;
    int title[MAX_TITLES];    // G_TITLE object index
    int dropdown[MAX_TITLES]; // matching dropdown box object index, -1 if the tree has fewer
    int16_t x[MAX_TITLES], w[MAX_TITLES];
    int16_t drop_x[MAX_TITLES], drop_y[MAX_TITLES], drop_w[MAX_TITLES], drop_h[MAX_TITLES];
    int16_t title_y, title_h;
} MenuLayout;

static int build_layout(MenuLayout *out) {
    int root, titles_parent, menus_parent;
    if (!menu_parts(&root, &titles_parent, &menus_parent)) {
        return 0;
    }

    int bar = obj_word(root, OBJECT_OFS_HEAD);

    // Object coordinates are parent-relative, so an absolute position is the sum down the chain.
    int16_t tx = (int16_t)(obj_word(root, OBJECT_OFS_X) + obj_word(bar, OBJECT_OFS_X) + obj_word(titles_parent, OBJECT_OFS_X));
    int16_t ty = (int16_t)(obj_word(root, OBJECT_OFS_Y) + obj_word(bar, OBJECT_OFS_Y) + obj_word(titles_parent, OBJECT_OFS_Y));
    int16_t mx = (int16_t)(obj_word(root, OBJECT_OFS_X) + obj_word(menus_parent, OBJECT_OFS_X));
    int16_t my = (int16_t)(obj_word(root, OBJECT_OFS_Y) + obj_word(menus_parent, OBJECT_OFS_Y));

    int titles[MAX_TITLES], drops[MAX_TITLES];
    int ntitles = collect_children(titles_parent, titles, MAX_TITLES);
    int ndrops = collect_children(menus_parent, drops, MAX_TITLES);

    out->count = ntitles;
    out->title_y = ty;
    out->title_h = obj_word(titles_parent, OBJECT_OFS_H);

    for (int i = 0; i < ntitles; i++) {
        out->title[i] = titles[i];
        out->x[i] = (int16_t)(tx + obj_word(titles[i], OBJECT_OFS_X));
        out->w[i] = obj_word(titles[i], OBJECT_OFS_W);

        if (i < ndrops) {
            out->dropdown[i] = drops[i];
            out->drop_x[i] = (int16_t)(mx + obj_word(drops[i], OBJECT_OFS_X));
            out->drop_y[i] = (int16_t)(my + obj_word(drops[i], OBJECT_OFS_Y));
            out->drop_w[i] = obj_word(drops[i], OBJECT_OFS_W);
            out->drop_h[i] = obj_word(drops[i], OBJECT_OFS_H);
        } else {
            out->dropdown[i] = -1;
        }
    }
    return ntitles > 0;
}

static int title_at(const MenuLayout *ml, int16_t x) {
    for (int i = 0; i < ml->count; i++) {
        if (x >= ml->x[i] && x < ml->x[i] + ml->w[i]) {
            return i;
        }
    }
    return -1;
}

// DRAWING

static void draw_bar(void) {
    MenuLayout ml;
    if (s_bar_h <= 0 || !build_layout(&ml)) {
        return;
    }

    aes_window_begin_overlay();
    aes_window_save_clip();
    if (aes_window_set_clip(0, 0, (int16_t)GUEST_SCREEN_W, s_bar_h)) {
        canvas_fill(0, 0, (int16_t)GUEST_SCREEN_W, s_bar_h, vdi_attr_palette_color(VDI_WHITE));

        int16_t text_y = (int16_t)((s_bar_h - 16) / 2);
        if (text_y < 0) {
            text_y = 0;
        }

        for (int i = 0; i < ml.count; i++) {
            unsigned int flags = m68k_read_memory_16(obj_at(ml.title[i]) + OBJECT_OFS_FLAGS);
            if (flags & OF_HIDETREE) {
                continue;
            }

            unsigned int state = obj_state(ml.title[i]);
            unsigned int spec = m68k_read_memory_32(obj_at(ml.title[i]) + OBJECT_OFS_SPEC);
            if (state & OS_SELECTED) {
                canvas_fill(ml.x[i], 0, ml.w[i], s_bar_h, vdi_attr_palette_color(VDI_BLACK));
            }
            if (spec != 0) {
                char text[64];
                guest_read_cstring(spec, text, sizeof(text));
                int16_t color = (state & OS_DISABLED) ? VDI_LWHITE : ((state & OS_SELECTED) ? VDI_WHITE : VDI_BLACK);
                canvas_text(ml.x[i], text_y, text, vdi_attr_palette_color(color));
            }
        }

        aes_window_clear_clip();
    }
    aes_window_restore_clip();
    aes_window_end_overlay();
}

static void draw_item(int idx, int16_t ax, int16_t ay, int highlighted) {
    int16_t w = obj_word(idx, OBJECT_OFS_W);
    int16_t h = obj_word(idx, OBJECT_OFS_H);
    if (w <= 0 || h <= 0) {
        return;
    }

    unsigned int state = obj_state(idx);
    unsigned int spec = m68k_read_memory_32(obj_at(idx) + OBJECT_OFS_SPEC);
    int disabled = (state & OS_DISABLED) != 0;

    int16_t color = disabled ? VDI_LWHITE : (highlighted ? VDI_WHITE : VDI_BLACK);

    canvas_fill(ax, ay, w, h, vdi_attr_palette_color(highlighted ? VDI_BLACK : VDI_WHITE));
    if (spec != 0) {
        char text[128];
        guest_read_cstring(spec, text, sizeof(text));
        canvas_text(ax, ay, text, vdi_attr_palette_color(color));
    }

    // GEM item strings leave the first character cell clear for the checkmark.
    if (state & OS_CHECKED) {
        unsigned int mark = vdi_attr_palette_color(color);
        canvas_line((int16_t)(ax + 2), (int16_t)(ay + h / 2), (int16_t)(ax + 3), (int16_t)(ay + h - 4), mark, 0);
        canvas_line((int16_t)(ax + 3), (int16_t)(ay + h - 4), (int16_t)(ax + 6), (int16_t)(ay + 3), mark, 0);
    }
}

static void draw_dropdown(const MenuLayout *ml, int slot, int items[], int16_t item_y[], int *nitems, int highlighted) {
    int box = ml->dropdown[slot];
    *nitems = 0;
    if (box < 0) {
        return;
    }

    aes_window_begin_overlay();
    aes_window_save_clip();
    if (aes_window_set_clip(ml->drop_x[slot], ml->drop_y[slot], ml->drop_w[slot], ml->drop_h[slot])) {
        // The box itself (border plus background) comes from the resource; the items are drawn over it.
        unsigned int spec = m68k_read_memory_32(obj_at(box) + OBJECT_OFS_SPEC);
        canvas_fill(ml->drop_x[slot], ml->drop_y[slot], ml->drop_w[slot], ml->drop_h[slot], vdi_attr_palette_color((int16_t)((spec >> 12) & 0x0F)));
        canvas_fill((int16_t)(ml->drop_x[slot] + 1), (int16_t)(ml->drop_y[slot] + 1), (int16_t)(ml->drop_w[slot] - 2), (int16_t)(ml->drop_h[slot] - 2), vdi_attr_palette_color((int16_t)(spec & 0x0F)));

        *nitems = collect_children(box, items, MAX_TITLES * 4);
        for (int i = 0; i < *nitems; i++) {
            int16_t ix = (int16_t)(ml->drop_x[slot] + obj_word(items[i], OBJECT_OFS_X));
            int16_t iy = (int16_t)(ml->drop_y[slot] + obj_word(items[i], OBJECT_OFS_Y));
            item_y[i] = iy;
            draw_item(items[i], ix, iy, items[i] == highlighted);
        }

        aes_window_clear_clip();
    }
    aes_window_restore_clip();
    aes_window_end_overlay();
}

// INTERACTION

static int item_at(const MenuLayout *ml, int slot, const int items[], int nitems, int16_t x, int16_t y) {
    if (slot < 0 || x < ml->drop_x[slot] || x >= ml->drop_x[slot] + ml->drop_w[slot]) {
        return -1;
    }

    for (int i = 0; i < nitems; i++) {
        int16_t iy = (int16_t)(ml->drop_y[slot] + obj_word(items[i], OBJECT_OFS_Y));
        int16_t ih = obj_word(items[i], OBJECT_OFS_H);
        if (y >= iy && y < iy + ih) {
            unsigned int state = obj_state(items[i]);
            unsigned int flags = m68k_read_memory_16(obj_at(items[i]) + OBJECT_OFS_FLAGS);
            // Disabled entries and separators are inert, exactly as on a real ST.
            if ((state & OS_DISABLED) || (flags & OF_HIDETREE)) {
                return -1;
            }
            return items[i];
        }
    }
    return -1;
}

int aes_menu_track(int16_t mx, int16_t my) {
    MenuLayout ml;
    if (s_menu_tree == 0 || !build_layout(&ml)) {
        return 0; // no menu installed; the click belongs to the guest
    }
    (void)my;

    int slot = title_at(&ml, mx);
    if (slot < 0) {
        return 1; // empty space in the bar; swallowed, as GEM does
    }

    void *saved = NULL;
    int open = -1;
    int hot = -1;
    int chosen = -1;
    int items[MAX_TITLES * 4];
    int16_t item_y[MAX_TITLES * 4];
    int nitems = 0;
    int sticky = 0;
    int prev_down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;

    for (;;) {
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_LBUTTONDOWN || m.message == WM_LBUTTONUP) {
                continue; // this loop owns the button; dispatching would re-enter the chrome handler
            }
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        if (tos_is_halted()) {
            break;
        }

        int lx, ly;
        aes_window_cursor_pos(&lx, &ly);
        int down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;

        if (ly < s_bar_h) {
            int over = title_at(&ml, (int16_t)lx);
            if (over >= 0) {
                // `open` has to survive so the switch below can un-highlight the title being left.
                slot = over;
            }
        }

        if (open != slot) {
            if (saved) {
                canvas_restore_area(saved);
                saved = NULL;
            }
            if (open >= 0) {
                obj_set_state(ml.title[open], obj_state(ml.title[open]) & ~OS_SELECTED);
            }

            obj_set_state(ml.title[slot], obj_state(ml.title[slot]) | OS_SELECTED);
            draw_bar();

            if (ml.dropdown[slot] >= 0) {
                saved = canvas_save_area(ml.drop_x[slot], ml.drop_y[slot], ml.drop_w[slot], ml.drop_h[slot]);
            }
            hot = -1;
            draw_dropdown(&ml, slot, items, item_y, &nitems, hot);
            open = slot;
        }

        int over_item = item_at(&ml, slot, items, nitems, (int16_t)lx, (int16_t)ly);
        if (over_item != hot) {
            hot = over_item;
            draw_dropdown(&ml, slot, items, item_y, &nitems, hot);
        }

        if (down != prev_down) {
            prev_down = down;
            if (!down) {
                if (hot >= 0) {
                    chosen = hot;
                    break;
                }
                sticky = 1; // released without picking anything: the menu stays down until the next click
            } else if (sticky) {
                if (hot >= 0) {
                    chosen = hot;
                    break;
                }
                if (ly >= s_bar_h) {
                    break; // clicked away from the menu entirely
                }
            }
        }

        Sleep(10);
    }

    if (saved) {
        canvas_restore_area(saved);
    }

    if (chosen >= 0) {
        aes_window_queue_message(AES_MN_SELECTED, ml.title[open], (int16_t)chosen, 0, 0, 0);
        log_write(LOG_API, "menu selection -> MN_SELECTED title=%d item=%d queued", ml.title[open], chosen);
    } else if (open >= 0) {
        obj_set_state(ml.title[open], obj_state(ml.title[open]) & ~OS_SELECTED);
        log_write(LOG_API, "menu dismissed without a selection");
    }
    draw_bar();

    return 1;
}

// AES ENTRY POINTS

// [FULL] Only flips the object's state bit; the checkmark reaches the screen when the dropdown is next drawn.
void aes_menu_icheck(const AesPB *pb) {
    int16_t item = aes_pb_intin(pb, 0);
    int16_t checked = aes_pb_intin(pb, 1);

    aes_pb_set_intout(pb, 0, 1);

    unsigned int tree = aes_pb_addrin(pb, 0);
    if (tree == 0 || item < 0 || item >= aes_object_tree_limit(tree)) {
        log_write(LOG_API, "menu_icheck(item=%d, checked=%d) -> accepted, no tree to update", item, checked);
        return;
    }

    unsigned int addr = aes_object_addr(tree, item);
    unsigned int state = m68k_read_memory_16(addr + OBJECT_OFS_STATE);
    state = checked ? (state | OS_CHECKED) : (state & ~OS_CHECKED);
    m68k_write_memory_16(addr + OBJECT_OFS_STATE, state);

    log_write(LOG_API, "menu_icheck(item=%d, checked=%d) -> object state now 0x%04X", item, checked, state);
}

// [FULL] As with menu_icheck(), the state bit is what matters; drawing follows on the next dropdown.
void aes_menu_ienable(const AesPB *pb) {
    int16_t item = aes_pb_intin(pb, 0);
    int16_t enable = aes_pb_intin(pb, 1);

    aes_pb_set_intout(pb, 0, 1);

    unsigned int tree = aes_pb_addrin(pb, 0);
    if (tree == 0 || item < 0 || item >= aes_object_tree_limit(tree)) {
        log_write(LOG_API, "menu_ienable(item=%d, enable=%d) -> accepted, no loaded resource to update", item, enable);
        return;
    }

    unsigned int addr = aes_object_addr(tree, item);
    unsigned int state = m68k_read_memory_16(addr + OBJECT_OFS_STATE);
    state = enable ? (state & ~OS_DISABLED) : (state | OS_DISABLED);
    m68k_write_memory_16(addr + OBJECT_OFS_STATE, state);

    log_write(LOG_API, "menu_ienable(item=%d, enable=%d) -> object state now 0x%04X", item, enable, state);
}

// [FULL] Redraws immediately, since this is how an application un-highlights a title after its dropdown closes.
void aes_menu_tnormal(const AesPB *pb) {
    int16_t title = aes_pb_intin(pb, 0);
    int16_t normal = aes_pb_intin(pb, 1);

    aes_pb_set_intout(pb, 0, 1);

    unsigned int tree = aes_pb_addrin(pb, 0);
    if (tree == 0 || title < 0 || title >= aes_object_tree_limit(tree)) {
        log_write(LOG_API, "menu_tnormal(title=%d, normal=%d) -> accepted, no loaded resource to update", title, normal);
        return;
    }

    unsigned int addr = aes_object_addr(tree, title);
    unsigned int state = m68k_read_memory_16(addr + OBJECT_OFS_STATE);
    state = normal ? (state & ~OS_SELECTED) : (state | OS_SELECTED);
    m68k_write_memory_16(addr + OBJECT_OFS_STATE, state);
    draw_bar();

    log_write(LOG_API, "menu_tnormal(title=%d, normal=%d) -> title redrawn %s", title, normal, normal ? "normal" : "highlighted");
}

// [FULL] Showing the bar reserves a strip that windows cannot cover; hiding it hands that strip back to the desktop.
void aes_menu_bar(const AesPB *pb) {
    unsigned int tree = aes_pb_addrin(pb, 0);
    int16_t show = aes_pb_intin(pb, 0);

    aes_pb_set_intout(pb, 0, 1);

    if (!show || tree == 0) {
        int16_t was_h = s_bar_h;
        s_menu_tree = 0;
        s_bar_h = 0;
        aes_window_set_menu_height(0);
        if (was_h > 0) {
            // Hand the strip back: repaint the desktop under it and let any window that now reaches it redraw.
            aes_window_overlay_finished(0, 0, (int16_t)GUEST_SCREEN_W, was_h);
        }
        log_write(LOG_API, "menu_bar(show=%d, tree=0x%08X) -> hidden, %d-pixel strip released", show, tree, was_h);
        return;
    }

    s_menu_tree = tree;

    int root, titles_parent, menus_parent;
    int16_t bar_h = 0;
    if (menu_parts(&root, &titles_parent, &menus_parent)) {
        bar_h = obj_word(obj_word(root, OBJECT_OFS_HEAD), OBJECT_OFS_H);
    }
    if (bar_h <= 0 || bar_h > 64) {
        bar_h = DEFAULT_BAR_H;
    }

    s_bar_h = bar_h;
    aes_window_set_menu_height(bar_h); // reserves the strip so windows can't cover the menu bar

    draw_bar();

    log_write(LOG_API, "menu_bar(show=%d, tree=0x%08X) -> drawn at 0,0,%d,%d", show, tree, GUEST_SCREEN_W, bar_h);
}

// [FULL] Writes over the resource's own string in place, so the replacement must fit the buffer the resource allocated, exactly as on TOS.
void aes_menu_text(const AesPB *pb) {
    int16_t item = aes_pb_intin(pb, 0);
    unsigned int text_addr = aes_pb_addrin(pb, 1);

    aes_pb_set_intout(pb, 0, 1);

    unsigned int tree = aes_pb_addrin(pb, 0);
    if (tree == 0 || item < 0 || item >= aes_object_tree_limit(tree)) {
        log_write(LOG_API, "menu_text(item=%d) -> accepted, no loaded resource to update", item);
        return;
    }

    unsigned int spec = m68k_read_memory_32(aes_object_addr(tree, item) + OBJECT_OFS_SPEC);
    if (spec == 0) {
        log_write(LOG_API, "menu_text(item=%d) -> accepted, item has no string to replace", item);
        return;
    }

    char text[128];
    guest_read_cstring(text_addr, text, sizeof(text));
    guest_write_cstring(spec, text, sizeof(text));

    log_write(LOG_API, "menu_text(item=%d, \"%s\") -> written", item, text);
}