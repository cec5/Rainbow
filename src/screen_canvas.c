#include <stdlib.h>
#include <string.h>
#include "screen_canvas.h"
#include "logger.h"
#include "tos_layer.h"

#define WINDOW_CLASS_NAME "RainbowScreen"

static HWND s_hwnd = NULL;
static HDC s_dc = NULL;
static HBITMAP s_bitmap = NULL;
static HBITMAP s_old_bitmap = NULL;
static int s_class_registered = 0;
static int s_in_click = 0;

static CanvasHooks s_hooks = {0};

void canvas_set_hooks(const CanvasHooks *hooks) {
    if (hooks) {
        s_hooks = *hooks;
    } else {
        memset(&s_hooks, 0, sizeof(s_hooks));
    }
}

// FONTS

static HFONT get_font(int bold, int italic, int small_font) {
    static HFONT cache[8];
    int idx = (bold ? 1 : 0) | (italic ? 2 : 0) | (small_font ? 4 : 0);
    if (!cache[idx]) {
        int cw = small_font ? CANVAS_SMALL_CHAR_W : CANVAS_CHAR_W;
        int ch = small_font ? CANVAS_SMALL_CHAR_H : CANVAS_CHAR_H;
        cache[idx] = CreateFontA(ch, cw, 0, 0, bold ? FW_BOLD : FW_NORMAL, italic ? TRUE : FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_RASTER_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, FIXED_PITCH | FF_MODERN, "Terminal");
    }
    return cache[idx];
}

// HOST WINDOW

static LRESULT CALLBACK canvas_wndproc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_CLOSE:
            // The single real window IS the screen, so closing it ends the session rather than any one GEM window.
            log_write(LOG_API, "screen window closed -> halting execution");
            tos_request_halt();
            return 0;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (s_dc) {
                RECT client;
                GetClientRect(hwnd, &client);
                SetStretchBltMode(hdc, COLORONCOLOR);
                StretchBlt(hdc, 0, 0, client.right - client.left, client.bottom - client.top, s_dc, 0, 0, GUEST_SCREEN_W, GUEST_SCREEN_H, SRCCOPY);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (!s_in_click && s_hooks.click) {
                int lx = (int)(short)LOWORD(lparam) / DISPLAY_SCALE;
                int ly = (int)(short)HIWORD(lparam) / DISPLAY_SCALE;
                s_in_click = 1;
                s_hooks.click((int16_t)lx, (int16_t)ly);
                s_in_click = 0;
            }
            return 0;
        }

        case WM_CHAR:
            if (s_hooks.key) {
                s_hooks.key(((unsigned int)lparam >> 16) & 0xFF, (unsigned int)wparam & 0xFF);
            }
            return 0;

        case WM_KEYDOWN:
            // Only the keys that never produce a WM_CHAR of their own; the rest arrive above with their ASCII attached.
            switch (wparam) {
                case VK_UP: case VK_DOWN: case VK_LEFT: case VK_RIGHT:
                case VK_HOME: case VK_INSERT: case VK_DELETE:
                case VK_F1: case VK_F2: case VK_F3: case VK_F4: case VK_F5:
                case VK_F6: case VK_F7: case VK_F8: case VK_F9: case VK_F10:
                    if (s_hooks.key) {
                        s_hooks.key(((unsigned int)lparam >> 16) & 0xFF, 0);
                    }
                    break;
                default:
                    break;
            }
            return DefWindowProcA(hwnd, msg, wparam, lparam);

        default:
            return DefWindowProcA(hwnd, msg, wparam, lparam);
    }
}

static void ensure_window_class(void) {
    if (s_class_registered) {
        return;
    }

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = canvas_wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = WINDOW_CLASS_NAME;
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    RegisterClassA(&wc);

    s_class_registered = 1;
}

void canvas_ensure(void) {
    if (s_hwnd) {
        return;
    }

    ensure_window_class();

    DWORD style = WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT wr = {0, 0, GUEST_SCREEN_W * DISPLAY_SCALE, GUEST_SCREEN_H * DISPLAY_SCALE};
    AdjustWindowRect(&wr, style, FALSE);
    int outer_w = wr.right - wr.left;
    int outer_h = wr.bottom - wr.top;

    int pos_x = (GetSystemMetrics(SM_CXSCREEN) - outer_w) / 2;
    int pos_y = (GetSystemMetrics(SM_CYSCREEN) - outer_h) / 2;
    if (pos_x < 0) pos_x = 0;
    if (pos_y < 0) pos_y = 0;

    s_hwnd = CreateWindowExA(0, WINDOW_CLASS_NAME, "GEM Screen", style, pos_x, pos_y, outer_w, outer_h, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!s_hwnd) {
        log_write(LOG_ERROR, "screen window -> CreateWindowExA failed, GetLastError=%lu", (unsigned long)GetLastError());
        return;
    }

    HDC screen_dc = GetDC(s_hwnd);
    s_dc = CreateCompatibleDC(screen_dc);
    s_bitmap = CreateCompatibleBitmap(screen_dc, GUEST_SCREEN_W, GUEST_SCREEN_H);
    s_old_bitmap = (HBITMAP)SelectObject(s_dc, s_bitmap);
    ReleaseDC(s_hwnd, screen_dc);
    SelectObject(s_dc, get_font(0, 0, 0));

    // A fresh bitmap is all zeros, which is black; give it a defined state before anyone sees it.
    canvas_fill(0, 0, GUEST_SCREEN_W, GUEST_SCREEN_H, RGB(0xFF, 0xFF, 0xFF));

    ShowWindow(s_hwnd, SW_SHOWNORMAL);
    UpdateWindow(s_hwnd);
    log_write(LOG_API, "screen created: one %dx%d logical canvas in a %dx%d real window at (%d,%d)", GUEST_SCREEN_W, GUEST_SCREEN_H, outer_w, outer_h, pos_x, pos_y);

    // Everything above is set, so a re-entrant canvas_ensure() from the hook returns immediately.
    if (s_hooks.created) {
        s_hooks.created();
    }
}

int canvas_ready(void) {
    return s_dc != NULL;
}

void canvas_pump_messages(void) {
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

HWND canvas_hwnd(void) {
    return s_hwnd;
}

void canvas_present(void) {
    if (!s_hwnd || !s_dc) {
        return;
    }
    RECT client;
    GetClientRect(s_hwnd, &client);
    HDC hdc = GetDC(s_hwnd);
    SetStretchBltMode(hdc, COLORONCOLOR);
    StretchBlt(hdc, 0, 0, client.right - client.left, client.bottom - client.top, s_dc, 0, 0, GUEST_SCREEN_W, GUEST_SCREEN_H, SRCCOPY);
    ReleaseDC(s_hwnd, hdc);
}

// Guest drawing only marks the window invalid; a run of primitives then costs one presentation rather than one each.
static void after_draw(void) {
    if (s_hwnd) {
        InvalidateRect(s_hwnd, NULL, FALSE);
    }
}

// COORDINATES

void canvas_real_to_guest(int *x, int *y) {
    canvas_ensure();
    POINT origin = {0, 0};
    if (s_hwnd) {
        ClientToScreen(s_hwnd, &origin);
    }
    *x = (*x - origin.x) / DISPLAY_SCALE;
    *y = (*y - origin.y) / DISPLAY_SCALE;
}

void canvas_cursor_pos(int *x, int *y) {
    POINT pt;
    GetCursorPos(&pt);
    *x = pt.x;
    *y = pt.y;
    canvas_real_to_guest(x, y);
}

// CLIPPING

void canvas_select_clip(HRGN rgn) {
    if (s_dc) {
        SelectClipRgn(s_dc, rgn);
    }
    if (rgn) {
        DeleteObject(rgn);
    }
}

void canvas_intersect_clip(HRGN rgn) {
    if (!rgn) {
        return;
    }
    if (s_dc) {
        ExtSelectClipRgn(s_dc, rgn, RGN_AND);
    }
    DeleteObject(rgn);
}

int canvas_set_clip(int16_t x, int16_t y, int16_t w, int16_t h) {
    canvas_ensure();
    if (s_hooks.set_clip) {
        return s_hooks.set_clip(x, y, w, h);
    }
    if (!s_dc || w <= 0 || h <= 0) {
        return 0;
    }
    canvas_select_clip(CreateRectRgn(x, y, x + w, y + h));
    return 1;
}

void canvas_clear_clip(void) {
    if (s_hooks.clear_clip) {
        s_hooks.clear_clip();
        return;
    }
    canvas_select_clip(NULL);
    canvas_present();
}

// PRIMITIVES

void canvas_fill(int16_t x, int16_t y, int16_t w, int16_t h, unsigned int color_rgb) {
    if (!s_dc || w <= 0 || h <= 0) {
        return;
    }
    RECT r = {x, y, x + w, y + h};
    HBRUSH brush = CreateSolidBrush((COLORREF)color_rgb);
    FillRect(s_dc, &r, brush);
    DeleteObject(brush);
    after_draw();
}

void canvas_invert(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!s_dc || w <= 0 || h <= 0) {
        return;
    }
    PatBlt(s_dc, x, y, w, h, DSTINVERT);
    after_draw();
}

void canvas_text(int16_t x, int16_t y, const char *text, unsigned int color_rgb) {
    if (!s_dc) {
        log_write(LOG_ERROR, "canvas text at %d,%d \"%s\" -> no screen canvas yet", x, y, text);
        return;
    }

    SetTextAlign(s_dc, TA_TOP | TA_LEFT); // AES chrome and object text position their own cells
    SetBkMode(s_dc, TRANSPARENT);
    SetTextColor(s_dc, (COLORREF)color_rgb);
    TextOutA(s_dc, x, y, text, (int)strlen(text));
    after_draw();
}

void canvas_text_font(int16_t x, int16_t y, const char *text, unsigned int color_rgb, int small_font) {
    if (!small_font) {
        canvas_text(x, y, text, color_rgb);
        return;
    }
    if (!s_dc) {
        log_write(LOG_ERROR, "canvas text at %d,%d \"%s\" -> no screen canvas yet", x, y, text);
        return;
    }

    HFONT old_font = (HFONT)SelectObject(s_dc, get_font(0, 0, 1));
    SetTextAlign(s_dc, TA_TOP | TA_LEFT);
    SetBkMode(s_dc, TRANSPARENT);
    SetTextColor(s_dc, (COLORREF)color_rgb);
    TextOutA(s_dc, x, y, text, (int)strlen(text));
    SelectObject(s_dc, old_font);
    after_draw();
}

void canvas_styled_text(int16_t x, int16_t y, const char *text, unsigned int color_rgb, int bold, int italic, int underline, int opaque, unsigned int bg_rgb, int halign, int valign) {
    if (!s_dc) {
        log_write(LOG_ERROR, "canvas styled text at %d,%d \"%s\" -> no screen canvas yet", x, y, text);
        return;
    }

    size_t len = strlen(text);
    HFONT old_font = (HFONT)SelectObject(s_dc, get_font(bold, italic, 0));

    TEXTMETRICA tm;
    GetTextMetricsA(s_dc, &tm);
    int ascent = tm.tmAscent - tm.tmInternalLeading;

    /* The caller's y names whichever line vst_alignment() selected, and the VDI
     * default is the baseline where GDI's is the top of the cell. Resolving to
     * a baseline here keeps the opaque cell and the underline in step with it. */
    int baseline = y;
    switch (valign) {
        case CANVAS_VALIGN_TOP:     baseline = y + tm.tmAscent; break;
        case CANVAS_VALIGN_ASCENT:  baseline = y + ascent; break;
        case CANVAS_VALIGN_HALF:    baseline = y + ascent / 2; break;
        case CANVAS_VALIGN_BOTTOM:
        case CANVAS_VALIGN_DESCENT: baseline = y - tm.tmDescent; break;
        default:                    break; // CANVAS_VALIGN_BASELINE
    }

    int text_w = (int)len * CANVAS_CHAR_W;
    int left = x;
    UINT align = TA_BASELINE;
    if (halign == CANVAS_HALIGN_CENTER) {
        align |= TA_CENTER;
        left = x - text_w / 2;
    } else if (halign == CANVAS_HALIGN_RIGHT) {
        align |= TA_RIGHT;
        left = x - text_w;
    } else {
        align |= TA_LEFT;
    }

    int cell_top = baseline - tm.tmAscent;

    UINT old_align = SetTextAlign(s_dc, align);
    SetBkMode(s_dc, TRANSPARENT);
    SetTextColor(s_dc, (COLORREF)color_rgb);

    if (opaque) {
        RECT cell;
        cell.left = left;
        cell.top = cell_top - 1;
        cell.right = left + text_w;
        cell.bottom = cell_top - 1 + CANVAS_CHAR_H;
        SetBkColor(s_dc, (COLORREF)bg_rgb);
        ExtTextOutA(s_dc, x, baseline, ETO_OPAQUE, &cell, text, (UINT)len, NULL);
    } else {
        TextOutA(s_dc, x, baseline, text, (int)len);
    }

    if (underline) {
        /* The face itself isn't underlined, so this is drawn. Position it from
         * the cell rather than the baseline: on the cell's last row it can't
         * spill into the row below and be erased by that row's own fill. */
        canvas_fill((int16_t)left, (int16_t)(cell_top + CANVAS_CHAR_H - 2), (int16_t)text_w, 1, color_rgb);
    }

    SetTextAlign(s_dc, old_align);
    SelectObject(s_dc, old_font);
    after_draw();
}

void canvas_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, unsigned int color_rgb, int xor_mode) {
    if (!s_dc) {
        log_write(LOG_ERROR, "canvas line %d,%d-%d,%d -> no screen canvas yet", x1, y1, x2, y2);
        return;
    }

    int old_rop = SetROP2(s_dc, xor_mode ? R2_NOT : R2_COPYPEN);
    HPEN pen = CreatePen(PS_SOLID, 1, (COLORREF)color_rgb);
    HPEN old_pen = (HPEN)SelectObject(s_dc, pen);
    MoveToEx(s_dc, x1, y1, NULL);
    LineTo(s_dc, x2, y2);

    // LineTo leaves the final pixel unset; SetPixel ignores the DC's ROP2, so the XOR case needs its own inversion.
    if (xor_mode) {
        PatBlt(s_dc, x2, y2, 1, 1, DSTINVERT);
    } else {
        SetPixel(s_dc, x2, y2, (COLORREF)color_rgb);
    }

    SelectObject(s_dc, old_pen);
    DeleteObject(pen);
    SetROP2(s_dc, old_rop);
    after_draw();
}

void canvas_blit(int16_t src_x, int16_t src_y, int16_t dst_x, int16_t dst_y, int16_t w, int16_t h, unsigned int rop) {
    if (!s_dc) {
        log_write(LOG_ERROR, "canvas blit src=%d,%d dst=%d,%d,%d,%d -> no screen canvas yet", src_x, src_y, dst_x, dst_y, w, h);
        return;
    }
    int overlaps = src_x < dst_x + w && dst_x < src_x + w && src_y < dst_y + h && dst_y < src_y + h;

    if (overlaps) {
        HDC bounce_dc = CreateCompatibleDC(s_dc);
        HBITMAP bounce_bmp = bounce_dc ? CreateCompatibleBitmap(s_dc, w, h) : NULL;

        if (bounce_dc && bounce_bmp) {
            HBITMAP old_bmp = (HBITMAP)SelectObject(bounce_dc, bounce_bmp);
            BitBlt(bounce_dc, 0, 0, w, h, s_dc, src_x, src_y, SRCCOPY);
            BitBlt(s_dc, dst_x, dst_y, w, h, bounce_dc, 0, 0, (DWORD)rop);
            SelectObject(bounce_dc, old_bmp);
        } else {
            log_write(LOG_ERROR, "canvas blit src=%d,%d dst=%d,%d,%d,%d -> off-screen bounce buffer allocation failed, falling back to a direct overlapping blit", src_x, src_y, dst_x, dst_y, w, h);
            BitBlt(s_dc, dst_x, dst_y, w, h, s_dc, src_x, src_y, (DWORD)rop);
        }

        if (bounce_bmp) DeleteObject(bounce_bmp);
        if (bounce_dc) DeleteDC(bounce_dc);
    } else {
        BitBlt(s_dc, dst_x, dst_y, w, h, s_dc, src_x, src_y, (DWORD)rop);
    }

    after_draw();
}

// AREA SNAPSHOTS

typedef struct {
    HDC dc;
    HBITMAP bitmap;
    HBITMAP old_bitmap;
    int16_t x, y, w, h;
} AreaSnapshot;

void *canvas_save_area(int16_t x, int16_t y, int16_t w, int16_t h) {
    canvas_ensure();
    if (!s_dc || w <= 0 || h <= 0) {
        return NULL;
    }

    AreaSnapshot *snap = (AreaSnapshot *)malloc(sizeof(*snap));
    if (!snap) {
        return NULL;
    }

    snap->dc = CreateCompatibleDC(s_dc);
    snap->bitmap = CreateCompatibleBitmap(s_dc, w, h);
    snap->old_bitmap = (HBITMAP)SelectObject(snap->dc, snap->bitmap);
    snap->x = x;
    snap->y = y;
    snap->w = w;
    snap->h = h;

    BitBlt(snap->dc, 0, 0, w, h, s_dc, x, y, SRCCOPY);
    return snap;
}

void canvas_restore_area(void *snapshot) {
    AreaSnapshot *snap = (AreaSnapshot *)snapshot;
    if (!snap) {
        return;
    }

    if (s_dc) {
        SelectClipRgn(s_dc, NULL);
        BitBlt(s_dc, snap->x, snap->y, snap->w, snap->h, snap->dc, 0, 0, SRCCOPY);
        canvas_present();
    }

    SelectObject(snap->dc, snap->old_bitmap);
    DeleteObject(snap->bitmap);
    DeleteDC(snap->dc);
    free(snap);
}