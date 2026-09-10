#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include "m68k.h"
#include "aes/aes_window.h"
#include "aes/aes_menu.h"
#include "aes/aes_object.h"
#include "guest_mem_util.h"
#include "screen_canvas.h"
#include "logger.h"
#include "screen.h"
#include "tos_layer.h"

#define MAX_WINDOWS 16

#define WF_NAME        2
#define WF_INFO        3
#define WF_WORKXYWH    4
#define WF_CURRXYWH    5
#define WF_FULLXYWH    7
#define WF_HSLIDE      8
#define WF_VSLIDE      9
#define WF_TOP         10
#define WF_FIRSTXYWH   11
#define WF_NEXTXYWH    12
#define WF_NEWDESK     14
#define WF_HSLSIZE     15
#define WF_VSLSIZE     16

#define WK_NAME    0x0001
#define WK_CLOSER  0x0002
#define WK_FULLER  0x0004
#define WK_MOVER   0x0008
#define WK_INFO    0x0010
#define WK_SIZER   0x0020
#define WK_VSLIDE  0x0100
#define WK_HSLIDE  0x0800

#define WIN_BORDER   1
#define WIN_TITLE_H 16
#define WIN_INFO_H  16
#define WIN_SLIDE   16

#define WIN_MIN_W   64
#define WIN_MIN_H   48


#define OWNER_MENU (-2) // Owner value meaning "the menu bar", which floats above every window and is clipped by none of them.

#define COL_FRAME    RGB(0x00, 0x00, 0x00)
#define COL_CHROME   RGB(0xFF, 0xFF, 0xFF)
#define COL_ACTIVE   RGB(0xC0, 0xC0, 0xC0)
#define COL_TROUGH   RGB(0xE0, 0xE0, 0xE0)
#define COL_DESKTOP  RGB(0xA8, 0xA8, 0xA8)

typedef struct {
    int in_use;   // wind_create'd
    int opened;   // wind_open'd, so it occupies screen space
    int16_t kind; // WK_* bitmask of which decorations this window asked for
    int16_t full_x, full_y, full_w, full_h;
    char title[128];
    char info[128];
    // GEM slider state: position 0..1000 along the trough, size 1..1000 as a fraction of it.
    int16_t hslide, vslide;
    int16_t hslsize, vslsize;
    int needs_redraw;
    int16_t dirty[4]; // work-area sub-rect the guest still owes us a repaint for
} AesWindow;

static AesWindow s_windows[MAX_WINDOWS];

typedef struct {
    int code;
    int handle;
    int16_t rect[4];
} AesMessage;

#define MSG_QUEUE_SIZE 32
static AesMessage s_msg_queue[MSG_QUEUE_SIZE];
static int s_msg_head = 0, s_msg_tail = 0;

void aes_window_queue_message(int code, int handle, int16_t a, int16_t b, int16_t c, int16_t d) {
    int next = (s_msg_tail + 1) % MSG_QUEUE_SIZE;
    if (next == s_msg_head) {
        return; // queue full, drop
    }
    s_msg_queue[s_msg_tail].code = code;
    s_msg_queue[s_msg_tail].handle = handle;
    s_msg_queue[s_msg_tail].rect[0] = a;
    s_msg_queue[s_msg_tail].rect[1] = b;
    s_msg_queue[s_msg_tail].rect[2] = c;
    s_msg_queue[s_msg_tail].rect[3] = d;
    s_msg_tail = next;
}

static int s_zorder[MAX_WINDOWS];
static int s_zcount = 0;

/* GEM always has a menu bar: the Desktop owns one when the application hasn't
 * installed its own, so WF_WORKXYWH always excludes a strip. Applications read
 * the work area before calling menu_bar(), so this has to be reserved from the
 * start; menu_bar(tree, 0) is the only thing that releases it. */
#define DEFAULT_MENU_H 19
static int16_t s_menu_height = DEFAULT_MENU_H;
static int s_overlay_depth = 0;

// Which window the draws between vs_clip(on) and vs_clip(off) belong to; -1 = none, 0 = desktop, else a handle.
static int s_active_handle = -1;
static int16_t s_last_clip_rect[4] = {0, 0, 0, 0};

// While set, clipping resolves to the desktop rather than to whichever window contains the rect, so the WF_NEWDESK tree can only ever paint on desktop pixels.
static int s_clip_to_desktop = 0;

#define KEY_QUEUE_SIZE 64
static unsigned int s_key_queue[KEY_QUEUE_SIZE];
static int s_key_head = 0, s_key_tail = 0;

static void push_key(unsigned int code) {
    int next = (s_key_tail + 1) % KEY_QUEUE_SIZE;
    if (next == s_key_head) {
        return; // queue full, drop
    }
    s_key_queue[s_key_tail] = code;
    s_key_tail = next;
}

int aes_window_poll_key(unsigned int *out_code) {
    if (s_key_head == s_key_tail) {
        return 0;
    }
    *out_code = s_key_queue[s_key_head];
    s_key_head = (s_key_head + 1) % KEY_QUEUE_SIZE;
    return 1;
}

// Drains the message queue first, then falls back to scanning for a window overdue a WM_REDRAW; the evnt_mesag/evnt_multi wait polls this once per loop iteration.
int aes_window_pump(int *out_code, int *out_handle, int16_t rect[4]) {
    canvas_pump_messages();

    if (s_msg_head != s_msg_tail) {
        AesMessage *m = &s_msg_queue[s_msg_head];
        s_msg_head = (s_msg_head + 1) % MSG_QUEUE_SIZE;
        *out_code = m->code;
        *out_handle = m->handle;
        rect[0] = m->rect[0];
        rect[1] = m->rect[1];
        rect[2] = m->rect[2];
        rect[3] = m->rect[3];
        return 1;
    }

    for (int i = 0; i < MAX_WINDOWS; i++) {
        AesWindow *win = &s_windows[i];
        if (!win->in_use) {
            continue;
        }

        if (win->needs_redraw) {
            win->needs_redraw = 0;
            *out_code = AES_WM_REDRAW;
            *out_handle = i + 1;
            rect[0] = win->dirty[0];
            rect[1] = win->dirty[1];
            rect[2] = win->dirty[2];
            rect[3] = win->dirty[3];
            return 1;
        }
    }

    return 0;
}

static AesWindow *get_window(int16_t handle) {
    if (handle < 1 || handle > MAX_WINDOWS) {
        return NULL;
    }
    AesWindow *win = &s_windows[handle - 1];
    return win->in_use ? win : NULL;
}

// GEOMETRY

static void chrome_insets(int16_t kind, int *left, int *top, int *right, int *bottom) {
    int border = kind ? WIN_BORDER : 0;

    *left = border;
    *right = border;
    *top = border;
    *bottom = border;

    if (kind & (WK_NAME | WK_CLOSER | WK_FULLER | WK_MOVER)) {
        *top += WIN_TITLE_H;
    }
    if (kind & WK_INFO) {
        *top += WIN_INFO_H;
    }
    if (kind & WK_VSLIDE) {
        *right += WIN_SLIDE;
    }
    if (kind & WK_HSLIDE) {
        *bottom += WIN_SLIDE;
    }
}

static void window_rect(const AesWindow *win, int work_area, int16_t out[4]) {
    if (!work_area) {
        out[0] = win->full_x;
        out[1] = win->full_y;
        out[2] = win->full_w;
        out[3] = win->full_h;
        return;
    }

    int l, t, r, b;
    chrome_insets(win->kind, &l, &t, &r, &b);
    out[0] = (int16_t)(win->full_x + l);
    out[1] = (int16_t)(win->full_y + t);
    out[2] = (int16_t)(win->full_w - l - r);
    out[3] = (int16_t)(win->full_h - t - b);
    if (out[2] < 0) out[2] = 0;
    if (out[3] < 0) out[3] = 0;
}

static void desktop_work_rect(int16_t out[4]) {
    out[0] = 0;
    out[1] = s_menu_height;
    out[2] = (int16_t)GUEST_SCREEN_W;
    out[3] = (int16_t)(GUEST_SCREEN_H - s_menu_height);
}

static int rect_contains(const int16_t r[4], int16_t x, int16_t y, int16_t w, int16_t h) {
    return x >= r[0] && y >= r[1] && x + w <= r[0] + r[2] && y + h <= r[1] + r[3];
}

// Z-ORDER

static int z_index_of(int handle) {
    for (int i = 0; i < s_zcount; i++) {
        if (s_zorder[i] == handle) {
            return i;
        }
    }
    return -1;
}

static void z_remove(int handle) {
    int at = z_index_of(handle);
    if (at < 0) {
        return;
    }
    for (int i = at; i < s_zcount - 1; i++) {
        s_zorder[i] = s_zorder[i + 1];
    }
    s_zcount--;
}

static void z_raise(int handle) {
    z_remove(handle);
    for (int i = s_zcount; i > 0; i--) {
        s_zorder[i] = s_zorder[i - 1];
    }
    s_zorder[0] = handle;
    s_zcount++;
}

static int z_top_handle(void) {
    return s_zcount > 0 ? s_zorder[0] : 0;
}

static void subtract_windows_above(HRGN rgn, int handle) {
    for (int i = 0; i < s_zcount; i++) {
        int h = s_zorder[i];
        if (h == handle) {
            return; // reached ourselves; the rest of the list is behind us
        }
        AesWindow *w = get_window((int16_t)h);
        if (!w || !w->opened) {
            continue;
        }
        HRGN above = CreateRectRgn(w->full_x, w->full_y, w->full_x + w->full_w, w->full_y + w->full_h);
        CombineRgn(rgn, rgn, above, RGN_DIFF);
        DeleteObject(above);
    }
}

static HRGN visible_region(int handle, int16_t x, int16_t y, int16_t w, int16_t h) {
    HRGN rgn = CreateRectRgn(x, y, x + w, y + h);
    if (handle == OWNER_MENU) {
        return rgn;
    }

    // handle 0 is the desktop, which is behind every window, so this subtracts all of them.
    subtract_windows_above(rgn, handle);

    if (s_menu_height > 0) {
        HRGN menu = CreateRectRgn(0, 0, GUEST_SCREEN_W, s_menu_height);
        CombineRgn(rgn, rgn, menu, RGN_DIFF);
        DeleteObject(menu);
    }
    return rgn;
}

// REDRAW BOOKKEEPING

static void mark_dirty(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (w <= 0 || h <= 0) {
        return;
    }

    for (int i = 0; i < MAX_WINDOWS; i++) {
        AesWindow *win = &s_windows[i];
        if (!win->in_use || !win->opened) {
            continue;
        }

        int16_t work[4];
        window_rect(win, 1, work);

        int16_t ix = x > work[0] ? x : work[0];
        int16_t iy = y > work[1] ? y : work[1];
        int16_t ir = (x + w < work[0] + work[2]) ? (int16_t)(x + w) : (int16_t)(work[0] + work[2]);
        int16_t ib = (y + h < work[1] + work[3]) ? (int16_t)(y + h) : (int16_t)(work[1] + work[3]);
        if (ir <= ix || ib <= iy) {
            continue;
        }

        if (!win->needs_redraw) {
            win->needs_redraw = 1;
            win->dirty[0] = ix;
            win->dirty[1] = iy;
            win->dirty[2] = (int16_t)(ir - ix);
            win->dirty[3] = (int16_t)(ib - iy);
        } else {
            int16_t ux = win->dirty[0] < ix ? win->dirty[0] : ix;
            int16_t uy = win->dirty[1] < iy ? win->dirty[1] : iy;
            int16_t ur = (win->dirty[0] + win->dirty[2]) > ir ? (int16_t)(win->dirty[0] + win->dirty[2]) : ir;
            int16_t ub = (win->dirty[1] + win->dirty[3]) > ib ? (int16_t)(win->dirty[1] + win->dirty[3]) : ib;
            win->dirty[0] = ux;
            win->dirty[1] = uy;
            win->dirty[2] = (int16_t)(ur - ux);
            win->dirty[3] = (int16_t)(ub - uy);
        }
    }
}

static void mark_window_dirty(int handle) {
    AesWindow *win = get_window((int16_t)handle);
    if (!win || !win->opened) {
        return;
    }
    int16_t work[4];
    window_rect(win, 1, work);
    win->needs_redraw = 1;
    win->dirty[0] = work[0];
    win->dirty[1] = work[1];
    win->dirty[2] = work[2];
    win->dirty[3] = work[3];
}

static unsigned int s_desktop_tree = 0; // object tree installed via wind_set(0, WF_NEWDESK, ...).

static void repaint_desktop_area(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (!canvas_ready()) {
        return;
    }
    canvas_select_clip(visible_region(0, x, y, w, h));
    canvas_fill(x, y, w, h, COL_DESKTOP);
    canvas_select_clip(NULL);

    if (s_desktop_tree != 0) {
        int16_t root_x = (int16_t)m68k_read_memory_16(s_desktop_tree + OBJECT_OFS_X);
        int16_t root_y = (int16_t)m68k_read_memory_16(s_desktop_tree + OBJECT_OFS_Y);
        s_clip_to_desktop = 1;
        aes_objc_draw_at(s_desktop_tree, s_desktop_tree, root_x, root_y, x, y, w, h, 8);
        s_clip_to_desktop = 0;
        aes_window_clear_clip();
    }
}

// CHROME

static void chrome_boxes(const AesWindow *win, RECT *title, RECT *closer, RECT *fuller) {
    int has_title = (win->kind & (WK_NAME | WK_CLOSER | WK_FULLER | WK_MOVER)) != 0;
    SetRect(title, 0, 0, 0, 0);
    SetRect(closer, 0, 0, 0, 0);
    SetRect(fuller, 0, 0, 0, 0);
    if (!has_title) {
        return;
    }

    int x = win->full_x + WIN_BORDER;
    int y = win->full_y + WIN_BORDER;
    int w = win->full_w - 2 * WIN_BORDER;
    SetRect(title, x, y, x + w, y + WIN_TITLE_H);

    if (win->kind & WK_CLOSER) {
        SetRect(closer, x, y, x + WIN_TITLE_H, y + WIN_TITLE_H);
    }
    if (win->kind & WK_FULLER) {
        SetRect(fuller, x + w - WIN_TITLE_H, y, x + w, y + WIN_TITLE_H);
    }
}

static int slider_boxes(const AesWindow *win, int vertical, RECT *up, RECT *trough, RECT *thumb, RECT *down) {
    SetRect(up, 0, 0, 0, 0);
    SetRect(down, 0, 0, 0, 0);
    SetRect(trough, 0, 0, 0, 0);
    SetRect(thumb, 0, 0, 0, 0);

    if (vertical ? !(win->kind & WK_VSLIDE) : !(win->kind & WK_HSLIDE)) {
        return 0;
    }

    int l, t, r, b;
    chrome_insets(win->kind, &l, &t, &r, &b);

    int start, end, cross;
    if (vertical) {
        cross = win->full_x + win->full_w - WIN_BORDER - WIN_SLIDE;
        start = win->full_y + t;
        end = win->full_y + win->full_h - WIN_BORDER - ((win->kind & WK_HSLIDE) ? WIN_SLIDE : 0);
    } else {
        cross = win->full_y + win->full_h - WIN_BORDER - WIN_SLIDE;
        start = win->full_x + l;
        end = win->full_x + win->full_w - WIN_BORDER - ((win->kind & WK_VSLIDE) ? WIN_SLIDE : 0);
    }

    int span = end - start;
    if (span < 3 * WIN_SLIDE) {
        return 0; // no room for two arrows and a usable trough
    }

    int16_t pos = vertical ? win->vslide : win->hslide;
    int16_t size = vertical ? win->vslsize : win->hslsize;
    if (pos < 0) pos = 0;
    if (pos > 1000) pos = 1000;
    if (size <= 0 || size > 1000) size = 1000; // GEM's -1 ("as large as the trough") and anything out of range

    int travel = span - 2 * WIN_SLIDE;
    int thumb_len = travel * size / 1000;
    if (thumb_len < 8) thumb_len = 8;
    if (thumb_len > travel) thumb_len = travel;
    int thumb_start = start + WIN_SLIDE + (travel - thumb_len) * pos / 1000;

    if (vertical) {
        SetRect(up, cross, start, cross + WIN_SLIDE, start + WIN_SLIDE);
        SetRect(down, cross, end - WIN_SLIDE, cross + WIN_SLIDE, end);
        SetRect(trough, cross, start + WIN_SLIDE, cross + WIN_SLIDE, end - WIN_SLIDE);
        SetRect(thumb, cross, thumb_start, cross + WIN_SLIDE, thumb_start + thumb_len);
    } else {
        SetRect(up, start, cross, start + WIN_SLIDE, cross + WIN_SLIDE);
        SetRect(down, end - WIN_SLIDE, cross, end, cross + WIN_SLIDE);
        SetRect(trough, start + WIN_SLIDE, cross, end - WIN_SLIDE, cross + WIN_SLIDE);
        SetRect(thumb, thumb_start, cross, thumb_start + thumb_len, cross + WIN_SLIDE);
    }
    return 1;
}

static void draw_frame_rect(int16_t x, int16_t y, int16_t w, int16_t h, COLORREF color) {
    canvas_fill(x, y, w, 1, color);
    canvas_fill(x, (int16_t)(y + h - 1), w, 1, color);
    canvas_fill(x, y, 1, h, color);
    canvas_fill((int16_t)(x + w - 1), y, 1, h, color);
}

enum { ARROW_UP, ARROW_DOWN, ARROW_LEFT, ARROW_RIGHT };

static void fill_rect_struct(const RECT *r, COLORREF color) {
    canvas_fill((int16_t)r->left, (int16_t)r->top, (int16_t)(r->right - r->left), (int16_t)(r->bottom - r->top), color);
}

static void draw_slider_box(const RECT *r, int arrow) {
    fill_rect_struct(r, COL_CHROME);
    draw_frame_rect((int16_t)r->left, (int16_t)r->top, (int16_t)(r->right - r->left), (int16_t)(r->bottom - r->top), COL_FRAME);

    // A solid triangle, widening a row (or column) at a time from the tip.
    int cx = (r->left + r->right) / 2;
    int cy = (r->top + r->bottom) / 2;
    for (int i = 0; i < 5; i++) {
        switch (arrow) {
            case ARROW_UP:    canvas_fill((int16_t)(cx - i), (int16_t)(cy - 2 + i), (int16_t)(2 * i + 1), 1, COL_FRAME); break;
            case ARROW_DOWN:  canvas_fill((int16_t)(cx - i), (int16_t)(cy + 2 - i), (int16_t)(2 * i + 1), 1, COL_FRAME); break;
            case ARROW_LEFT:  canvas_fill((int16_t)(cx - 2 + i), (int16_t)(cy - i), 1, (int16_t)(2 * i + 1), COL_FRAME); break;
            case ARROW_RIGHT: canvas_fill((int16_t)(cx + 2 - i), (int16_t)(cy - i), 1, (int16_t)(2 * i + 1), COL_FRAME); break;
            default: break;
        }
    }
}

static void draw_window_frame(int handle) {
    AesWindow *win = get_window((int16_t)handle);
    if (!win || !win->opened || !canvas_ready() || !win->kind) {
        return;
    }

    canvas_select_clip(visible_region(handle, win->full_x, win->full_y, win->full_w, win->full_h));

    int is_top = (z_top_handle() == handle);

    int l, t, r, b;
    chrome_insets(win->kind, &l, &t, &r, &b);

    // The decoration band around the work area, then the frame outline on top of it.
    canvas_fill(win->full_x, win->full_y, win->full_w, (int16_t)t, COL_CHROME);
    canvas_fill(win->full_x, (int16_t)(win->full_y + win->full_h - b), win->full_w, (int16_t)b, COL_TROUGH);
    canvas_fill(win->full_x, win->full_y, (int16_t)l, win->full_h, COL_CHROME);
    canvas_fill((int16_t)(win->full_x + win->full_w - r), win->full_y, (int16_t)r, win->full_h, COL_TROUGH);
    draw_frame_rect(win->full_x, win->full_y, win->full_w, win->full_h, COL_FRAME);

    RECT title, closer, fuller;
    chrome_boxes(win, &title, &closer, &fuller);

    if (title.right > title.left) {
        canvas_fill((int16_t)title.left, (int16_t)title.top, (int16_t)(title.right - title.left), (int16_t)(title.bottom - title.top), is_top ? COL_ACTIVE : COL_CHROME);
        canvas_fill((int16_t)title.left, (int16_t)title.bottom, (int16_t)(title.right - title.left), 1, COL_FRAME);

        if (win->title[0]) {
            int text_left = closer.right > closer.left ? closer.right : title.left;
            int text_right = fuller.right > fuller.left ? fuller.left : title.right;
            int avail = text_right - text_left;
            int text_w = (int)strlen(win->title) * 8;
            int tx = text_left + (avail - text_w) / 2;
            if (tx < text_left) tx = text_left;

            canvas_intersect_clip(CreateRectRgn(text_left, title.top, text_right, title.bottom));
            canvas_text((int16_t)tx, (int16_t)title.top, win->title, COL_FRAME);
            canvas_select_clip(visible_region(handle, win->full_x, win->full_y, win->full_w, win->full_h));
        }

        if (closer.right > closer.left) {
            draw_frame_rect((int16_t)closer.left, (int16_t)closer.top, WIN_TITLE_H, WIN_TITLE_H, COL_FRAME);
            canvas_fill((int16_t)(closer.left + 5), (int16_t)(closer.top + 5), 6, 6, COL_FRAME);
        }
        if (fuller.right > fuller.left) {
            draw_frame_rect((int16_t)fuller.left, (int16_t)fuller.top, WIN_TITLE_H, WIN_TITLE_H, COL_FRAME);
            draw_frame_rect((int16_t)(fuller.left + 4), (int16_t)(fuller.top + 4), 8, 8, COL_FRAME);
        }
    }

    for (int vertical = 0; vertical < 2; vertical++) {
        RECT up, trough, thumb, down;
        if (!slider_boxes(win, vertical, &up, &trough, &thumb, &down)) {
            continue;
        }
        draw_slider_box(&up, vertical ? ARROW_UP : ARROW_LEFT);
        draw_slider_box(&down, vertical ? ARROW_DOWN : ARROW_RIGHT);
        fill_rect_struct(&trough, COL_TROUGH);
        fill_rect_struct(&thumb, COL_ACTIVE);
        draw_frame_rect((int16_t)thumb.left, (int16_t)thumb.top, (int16_t)(thumb.right - thumb.left), (int16_t)(thumb.bottom - thumb.top), COL_FRAME);
    }

    if (win->kind & WK_SIZER) {
        int16_t sx = (int16_t)(win->full_x + win->full_w - WIN_BORDER - WIN_SLIDE);
        int16_t sy = (int16_t)(win->full_y + win->full_h - WIN_BORDER - WIN_SLIDE);
        canvas_fill(sx, sy, WIN_SLIDE, WIN_SLIDE, COL_CHROME);
        draw_frame_rect(sx, sy, WIN_SLIDE, WIN_SLIDE, COL_FRAME);
        // Two nested corners, the shorthand a real GEM size box uses.
        canvas_fill((int16_t)(sx + 4), (int16_t)(sy + 4), 8, 1, COL_FRAME);
        canvas_fill((int16_t)(sx + 4), (int16_t)(sy + 4), 1, 8, COL_FRAME);
    }

    canvas_select_clip(NULL);
}

static void redraw_all_frames(void) {
    // Back to front, so a window in front overwrites the one behind it.
    for (int i = s_zcount - 1; i >= 0; i--) {
        draw_window_frame(s_zorder[i]);
    }
}

// CHROME INTERACTION

void aes_window_cursor_pos(int *x, int *y) {
    canvas_cursor_pos(x, y);
}

// h == 0 releases the strip entirely, which is what hiding the menu bar does.
void aes_window_set_menu_height(int16_t h) {
    if (h >= 0 && h != s_menu_height) {
        s_menu_height = h;
    }
}

int16_t aes_window_menu_height(void) {
    return s_menu_height;
}

static void wait_for_button_release(void) {
    // Swallows the rest of a click we handled ourselves so the guest's own edge detector never sees it.
    while (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
        if (tos_is_halted()) {
            return;
        }
        MSG m;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_LBUTTONUP) {
                return;
            }
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        Sleep(5);
    }
}

static void drag_outline(int handle, int grab_dx, int grab_dy, int resizing, int16_t out[4]) {
    AesWindow *win = get_window((int16_t)handle);
    if (!win) {
        return;
    }

    HWND hwnd = canvas_hwnd();
    HDC hdc = GetDC(hwnd);
    int old_rop = SetROP2(hdc, R2_NOT);
    HBRUSH old_brush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HPEN old_pen = (HPEN)SelectObject(hdc, pen);

    int cur_x = win->full_x, cur_y = win->full_y;
    int cur_w = win->full_w, cur_h = win->full_h;
    int drawn = 0;

    for (;;) {
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000) || tos_is_halted()) {
            break;
        }

        int lx, ly;
        aes_window_cursor_pos(&lx, &ly);

        int nx = cur_x, ny = cur_y, nw = cur_w, nh = cur_h;
        if (resizing) {
            nw = lx - grab_dx - win->full_x + 1;
            nh = ly - grab_dy - win->full_y + 1;
            if (nw < WIN_MIN_W) nw = WIN_MIN_W;
            if (nh < WIN_MIN_H) nh = WIN_MIN_H;
            if (win->full_x + nw > GUEST_SCREEN_W) nw = GUEST_SCREEN_W - win->full_x;
            if (win->full_y + nh > GUEST_SCREEN_H) nh = GUEST_SCREEN_H - win->full_y;
        } else {
            nx = lx - grab_dx;
            ny = ly - grab_dy;
            if (ny < s_menu_height) ny = s_menu_height;
            if (nx < -(win->full_w - WIN_TITLE_H)) nx = -(win->full_w - WIN_TITLE_H);
            if (nx > GUEST_SCREEN_W - WIN_TITLE_H) nx = GUEST_SCREEN_W - WIN_TITLE_H;
            if (ny > GUEST_SCREEN_H - WIN_TITLE_H) ny = GUEST_SCREEN_H - WIN_TITLE_H;
        }

        if (!drawn || nx != cur_x || ny != cur_y || nw != cur_w || nh != cur_h) {
            if (drawn) {
                Rectangle(hdc, cur_x * DISPLAY_SCALE, cur_y * DISPLAY_SCALE, (cur_x + cur_w) * DISPLAY_SCALE, (cur_y + cur_h) * DISPLAY_SCALE);
            }
            cur_x = nx;
            cur_y = ny;
            cur_w = nw;
            cur_h = nh;
            Rectangle(hdc, cur_x * DISPLAY_SCALE, cur_y * DISPLAY_SCALE, (cur_x + cur_w) * DISPLAY_SCALE, (cur_y + cur_h) * DISPLAY_SCALE);
            drawn = 1;
        }

        MSG m;
        int released = 0;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_LBUTTONUP) {
                released = 1;
                break;
            }
            if (m.message == WM_PAINT || m.message == WM_MOUSEMOVE) {
                continue; // repainting mid-drag would fight the XOR outline
            }
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        if (released) {
            break;
        }
        Sleep(5);
    }

    if (drawn) {
        Rectangle(hdc, cur_x * DISPLAY_SCALE, cur_y * DISPLAY_SCALE, (cur_x + cur_w) * DISPLAY_SCALE, (cur_y + cur_h) * DISPLAY_SCALE);
    }
    SelectObject(hdc, old_pen);
    DeleteObject(pen);
    SelectObject(hdc, old_brush);
    SetROP2(hdc, old_rop);
    ReleaseDC(hwnd, hdc);

    out[0] = (int16_t)cur_x;
    out[1] = (int16_t)cur_y;
    out[2] = (int16_t)cur_w;
    out[3] = (int16_t)cur_h;
}

// wind_get(WF_?SLIDE) message arguments, in the order the AES numbers them.
#define WA_UPPAGE 0
#define WA_DNPAGE 1
#define WA_UPLINE 2
#define WA_DNLINE 3
#define WA_LFPAGE 4
#define WA_RTPAGE 5
#define WA_LFLINE 6
#define WA_RTLINE 7

static int point_in(const RECT *r, int16_t x, int16_t y) {
    return r->right > r->left && x >= r->left && x < r->right && y >= r->top && y < r->bottom;
}

static int drag_thumb(const AesWindow *win, int vertical, const RECT *trough, const RECT *thumb, int grab) {
    int thumb_len = vertical ? (thumb->bottom - thumb->top) : (thumb->right - thumb->left);
    int trough_start = vertical ? trough->top : trough->left;
    int trough_len = vertical ? (trough->bottom - trough->top) : (trough->right - trough->left);
    int travel = trough_len - thumb_len;
    (void)win;

    int pos = 0;
    while ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) && !tos_is_halted()) {
        MSG m;
        int released = 0;
        while (PeekMessageA(&m, NULL, 0, 0, PM_REMOVE)) {
            if (m.message == WM_LBUTTONUP) {
                released = 1;
                break;
            }
            if (m.message == WM_LBUTTONDOWN) {
                continue;
            }
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }

        int lx, ly;
        aes_window_cursor_pos(&lx, &ly);
        int offset = (vertical ? ly : lx) - grab - trough_start;
        if (offset < 0) offset = 0;
        if (offset > travel) offset = travel;
        pos = travel > 0 ? offset * 1000 / travel : 0;

        if (released) {
            break;
        }
        Sleep(10);
    }
    return pos;
}

static int handle_slider_click(int handle, AesWindow *win, int16_t lx, int16_t ly) {
    for (int vertical = 0; vertical < 2; vertical++) {
        RECT up, trough, thumb, down;
        if (!slider_boxes(win, vertical, &up, &trough, &thumb, &down)) {
            continue;
        }

        if (point_in(&up, lx, ly)) {
            wait_for_button_release();
            aes_window_queue_message(AES_WM_ARROWED, handle, vertical ? WA_UPLINE : WA_LFLINE, 0, 0, 0);
            return 1;
        }
        if (point_in(&down, lx, ly)) {
            wait_for_button_release();
            aes_window_queue_message(AES_WM_ARROWED, handle, vertical ? WA_DNLINE : WA_RTLINE, 0, 0, 0);
            return 1;
        }
        if (point_in(&thumb, lx, ly)) {
            int grab = vertical ? (ly - thumb.top) : (lx - thumb.left);
            int pos = drag_thumb(win, vertical, &trough, &thumb, grab);
            aes_window_queue_message(vertical ? AES_WM_VSLID : AES_WM_HSLID, handle, (int16_t)pos, 0, 0, 0);
            log_write(LOG_API, "window %d %s slider dragged -> position %d/1000", handle, vertical ? "vertical" : "horizontal", pos);
            return 1;
        }
        if (point_in(&trough, lx, ly)) {
            // Above or below the thumb pages rather than lines, as GEM does.
            int before = vertical ? (ly < thumb.top) : (lx < thumb.left);
            wait_for_button_release();
            aes_window_queue_message(AES_WM_ARROWED, handle, vertical ? (before ? WA_UPPAGE : WA_DNPAGE) : (before ? WA_LFPAGE : WA_RTPAGE), 0, 0, 0);
            return 1;
        }
    }
    return 0;
}

static int window_at(int16_t x, int16_t y) {
    for (int i = 0; i < s_zcount; i++) {
        AesWindow *win = get_window((int16_t)s_zorder[i]);
        if (!win || !win->opened) {
            continue;
        }
        if (x >= win->full_x && y >= win->full_y && x < win->full_x + win->full_w && y < win->full_y + win->full_h) {
            return s_zorder[i];
        }
    }
    return 0;
}

static int handle_chrome_click(int16_t lx, int16_t ly) {
    if (ly < s_menu_height) {
        return aes_menu_track(lx, ly);
    }

    int handle = window_at(lx, ly);
    if (handle == 0) {
        return 0;
    }

    AesWindow *win = get_window((int16_t)handle);
    if (!win) {
        return 0;
    }

    if (z_top_handle() != handle) {
        z_raise(handle);
        aes_window_queue_message(AES_WM_TOPPED, handle, 0, 0, 0, 0);
        mark_window_dirty(handle);
        redraw_all_frames();
        canvas_present();
    }

    RECT title, closer, fuller;
    chrome_boxes(win, &title, &closer, &fuller);

    if (closer.right > closer.left && lx >= closer.left && lx < closer.right && ly >= closer.top && ly < closer.bottom) {
        wait_for_button_release();
        aes_window_queue_message(AES_WM_CLOSED, handle, 0, 0, 0, 0);
        log_write(LOG_API, "window %d closer clicked -> WM_CLOSED queued", handle);
        return 1;
    }

    if (fuller.right > fuller.left && lx >= fuller.left && lx < fuller.right && ly >= fuller.top && ly < fuller.bottom) {
        wait_for_button_release();
        aes_window_queue_message(AES_WM_FULLED, handle, 0, 0, 0, 0);
        log_write(LOG_API, "window %d fuller clicked -> WM_FULLED queued", handle);
        return 1;
    }

    if ((win->kind & WK_SIZER) &&
        lx >= win->full_x + win->full_w - WIN_BORDER - WIN_SLIDE && lx < win->full_x + win->full_w &&
        ly >= win->full_y + win->full_h - WIN_BORDER - WIN_SLIDE && ly < win->full_y + win->full_h) {
        int16_t r[4];
        drag_outline(handle, lx - (win->full_x + win->full_w - 1), ly - (win->full_y + win->full_h - 1), 1, r);
        if (r[2] != win->full_w || r[3] != win->full_h) {
            aes_window_queue_message(AES_WM_SIZED, handle, r[0], r[1], r[2], r[3]);
            log_write(LOG_API, "window %d size box dragged -> WM_SIZED %d,%d,%d,%d queued", handle, r[0], r[1], r[2], r[3]);
        }
        return 1;
    }

    if ((win->kind & WK_MOVER) && title.right > title.left && lx >= title.left && lx < title.right && ly >= title.top && ly < title.bottom) {
        int16_t r[4];
        drag_outline(handle, lx - win->full_x, ly - win->full_y, 0, r);
        if (r[0] != win->full_x || r[1] != win->full_y) {
            aes_window_queue_message(AES_WM_MOVED, handle, r[0], r[1], r[2], r[3]);
            log_write(LOG_API, "window %d title dragged -> WM_MOVED %d,%d,%d,%d queued", handle, r[0], r[1], r[2], r[3]);
        }
        return 1;
    }

    int16_t work[4];
    window_rect(win, 1, work);
    if (lx >= work[0] && ly >= work[1] && lx < work[0] + work[2] && ly < work[1] + work[3]) {
        return 0; // work area: the guest's click
    }

    if (handle_slider_click(handle, win, lx, ly)) {
        return 1;
    }

    return 1;
}

// CANVAS HOOKS
// What the canvas has to ask the AES: which window owns a rect, and where clicks and keys go.

static void on_canvas_click(int16_t x, int16_t y) {
    handle_chrome_click(x, y);
}

static void on_canvas_key(unsigned int scan, unsigned int ascii) {
    push_key((scan << 8) | (ascii & 0xFF));
}

// The canvas can't paint the desktop itself: its colour and its object tree are the AES's.
static void on_canvas_created(void) {
    repaint_desktop_area(0, 0, (int16_t)GUEST_SCREEN_W, (int16_t)GUEST_SCREEN_H);
    canvas_present();
}

void aes_window_init(void) {
    static const CanvasHooks hooks = {
        aes_window_set_clip,
        aes_window_clear_clip,
        on_canvas_click,
        on_canvas_key,
        on_canvas_created,
    };
    canvas_set_hooks(&hooks);
}

// AES ENTRY POINTS

// [FULL] Reserves a bookkeeping slot only; no screen space is taken until wind_open().
void aes_wind_create(const AesPB *pb) {
    int16_t kind = aes_pb_intin(pb, 0);
    int16_t xfull = aes_pb_intin(pb, 1);
    int16_t yfull = aes_pb_intin(pb, 2);
    int16_t wfull = aes_pb_intin(pb, 3);
    int16_t hfull = aes_pb_intin(pb, 4);

    int handle = -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        if (!s_windows[i].in_use) {
            memset(&s_windows[i], 0, sizeof(s_windows[i]));
            s_windows[i].in_use = 1;
            s_windows[i].kind = kind;
            s_windows[i].full_x = xfull;
            s_windows[i].full_y = yfull;
            s_windows[i].full_w = wfull;
            s_windows[i].full_h = hfull;
            s_windows[i].hslsize = 1000;
            s_windows[i].vslsize = 1000;
            handle = i + 1;
            break;
        }
    }

    aes_pb_set_intout(pb, 0, (int16_t)handle);
    log_write(LOG_API, "wind_create(kind=0x%04X, %d,%d,%d,%d) -> handle=%d (no screen space taken until wind_open)", (uint16_t)kind, xfull, yfull, wfull, hfull, handle);
}

// [FULL] Opening raises the window to the top, matching GEM, where a newly opened window always becomes the active one.
void aes_wind_open(const AesPB *pb) {
    int16_t handle = aes_pb_intin(pb, 0);
    int16_t x = aes_pb_intin(pb, 1);
    int16_t y = aes_pb_intin(pb, 2);
    int16_t w = aes_pb_intin(pb, 3);
    int16_t h = aes_pb_intin(pb, 4);

    AesWindow *win = get_window(handle);
    if (!win) {
        aes_pb_set_intout(pb, 0, 0);
        log_write(LOG_ERROR, "wind_open(handle=%d) -> invalid handle", handle);
        return;
    }

    canvas_ensure();

    win->full_x = x;
    win->full_y = y;
    win->full_w = w;
    win->full_h = h;
    win->opened = 1;

    z_raise(handle);
    mark_window_dirty(handle);
    redraw_all_frames();
    canvas_present();

    aes_pb_set_intout(pb, 0, 1);
    log_write(LOG_API, "wind_open(handle=%d) -> occupies %d,%d,%d,%d, topmost of %d open window(s)", handle, x, y, w, h, s_zcount);
}

// [FULL] The handle stays allocated after closing, so the application can reopen the same window without creating it again.
void aes_wind_close(const AesPB *pb) {
    int16_t handle = aes_pb_intin(pb, 0);

    AesWindow *win = get_window(handle);
    if (!win) {
        aes_pb_set_intout(pb, 0, 0);
        log_write(LOG_ERROR, "wind_close(handle=%d) -> invalid handle", handle);
        return;
    }

    int16_t ox = win->full_x, oy = win->full_y, ow = win->full_w, oh = win->full_h;
    win->opened = 0;
    win->needs_redraw = 0;
    z_remove(handle);

    repaint_desktop_area(ox, oy, ow, oh);
    mark_dirty(ox, oy, ow, oh);
    redraw_all_frames();
    canvas_present();

    aes_pb_set_intout(pb, 0, 1);
    log_write(LOG_API, "wind_close(handle=%d) -> screen space at %d,%d,%d,%d released", handle, ox, oy, ow, oh);
}

// [FULL] Closes the window first if the application skipped wind_close(), since the screen space has to be released either way.
void aes_wind_delete(const AesPB *pb) {
    int16_t handle = aes_pb_intin(pb, 0);

    AesWindow *win = get_window(handle);
    if (!win) {
        aes_pb_set_intout(pb, 0, 0);
        log_write(LOG_ERROR, "wind_delete(handle=%d) -> invalid handle", handle);
        return;
    }

    if (win->opened) {
        int16_t ox = win->full_x, oy = win->full_y, ow = win->full_w, oh = win->full_h;
        win->opened = 0;
        z_remove(handle);
        repaint_desktop_area(ox, oy, ow, oh);
        mark_dirty(ox, oy, ow, oh);
        redraw_all_frames();
        canvas_present();
    }

    z_remove(handle);
    win->in_use = 0;

    aes_pb_set_intout(pb, 0, 1);
    log_write(LOG_API, "wind_delete(handle=%d) -> handle released", handle);
}

static RECT s_rect_list[64];
static int s_rect_count = 0;
static int s_rect_next = 0;

static void build_rect_list(int handle) {
    s_rect_count = 0;
    s_rect_next = 0;

    int16_t work[4];
    if (handle == 0) {
        desktop_work_rect(work);
    } else {
        AesWindow *win = get_window((int16_t)handle);
        if (!win || !win->opened) {
            return;
        }
        window_rect(win, 1, work);
    }

    HRGN rgn = visible_region(handle, work[0], work[1], work[2], work[3]);

    DWORD size = GetRegionData(rgn, 0, NULL);
    if (size > 0) {
        RGNDATA *data = (RGNDATA *)malloc(size);
        if (data && GetRegionData(rgn, size, data) == size) {
            RECT *rects = (RECT *)data->Buffer;
            int n = (int)data->rdh.nCount;
            if (n > (int)(sizeof(s_rect_list) / sizeof(s_rect_list[0]))) {
                n = (int)(sizeof(s_rect_list) / sizeof(s_rect_list[0]));
            }
            for (int i = 0; i < n; i++) {
                s_rect_list[i] = rects[i];
            }
            s_rect_count = n;
        }
        free(data);
    }

    DeleteObject(rgn);
}

// [PARTIAL] Covers the geometry, slider and rectangle-list fields an application needs to redraw itself; any other WF_ field is logged and answered with zeros.
void aes_wind_get(const AesPB *pb) {
    int16_t handle = aes_pb_intin(pb, 0);
    int16_t field = aes_pb_intin(pb, 1);

    int16_t out[4] = {0, 0, 0, 0};
    const char *source = "no data for this field";

    if (field == WF_TOP) {
        out[0] = (int16_t)z_top_handle();
        source = "topmost window handle";
    } else if (field == WF_FIRSTXYWH || field == WF_NEXTXYWH) {
        if (field == WF_FIRSTXYWH) {
            build_rect_list(handle);
        }
        if (s_rect_next < s_rect_count) {
            RECT *r = &s_rect_list[s_rect_next++];
            out[0] = (int16_t)r->left;
            out[1] = (int16_t)r->top;
            out[2] = (int16_t)(r->right - r->left);
            out[3] = (int16_t)(r->bottom - r->top);
            source = "visible rectangle list";
        } else {
            source = "visible rectangle list exhausted";
        }
    } else if (handle == 0) {
        if (field == WF_WORKXYWH) {
            desktop_work_rect(out);
            source = "desktop work area (screen minus menu bar)";
        } else if (field == WF_CURRXYWH || field == WF_FULLXYWH) {
            out[2] = (int16_t)GUEST_SCREEN_W;
            out[3] = (int16_t)GUEST_SCREEN_H;
            source = "whole guest screen";
        }
    } else {
        AesWindow *win = get_window(handle);
        if (win) {
            switch (field) {
                case WF_WORKXYWH:
                    window_rect(win, 1, out);
                    source = "work area";
                    break;

                case WF_CURRXYWH:
                    window_rect(win, 0, out);
                    source = "current full rect";
                    break;

                case WF_FULLXYWH:
                    desktop_work_rect(out);
                    source = "largest size this window could take";
                    break;

                case WF_HSLIDE: out[0] = win->hslide;  source = "horizontal slider position"; break;
                case WF_VSLIDE: out[0] = win->vslide;  source = "vertical slider position"; break;
                case WF_HSLSIZE: out[0] = win->hslsize; source = "horizontal slider size"; break;
                case WF_VSLSIZE: out[0] = win->vslsize; source = "vertical slider size"; break;

                default:
                    log_write(LOG_ERROR, "wind_get(handle=%d): unhandled field 0x%02X", handle, field);
                    break;
            }
        } else {
            log_write(LOG_ERROR, "wind_get(handle=%d): invalid handle", handle);
        }
    }

    aes_pb_set_intout(pb, 0, 1);
    aes_pb_set_intout(pb, 1, out[0]);
    aes_pb_set_intout(pb, 2, out[1]);
    aes_pb_set_intout(pb, 3, out[2]);
    aes_pb_set_intout(pb, 4, out[3]);

    log_write(LOG_API, "wind_get(handle=%d, field=0x%02X) -> %s -> %d,%d,%d,%d", handle, field, source, out[0], out[1], out[2], out[3]);
}

// [FULL] Pure arithmetic on the chrome insets for a window kind; no window needs to exist for it to answer.
void aes_wind_calc(const AesPB *pb) {
    int16_t wc = aes_pb_intin(pb, 0); // 0 = work->full, 1 = full->work
    int16_t kind = aes_pb_intin(pb, 1);
    int16_t x = aes_pb_intin(pb, 2);
    int16_t y = aes_pb_intin(pb, 3);
    int16_t w = aes_pb_intin(pb, 4);
    int16_t h = aes_pb_intin(pb, 5);

    int left, top, right, bottom;
    chrome_insets(kind, &left, &top, &right, &bottom);

    int16_t xo, yo, wo, ho;
    if (wc == 0) {
        // work -> full: grow outward to make room for the decorations
        xo = (int16_t)(x - left);
        yo = (int16_t)(y - top);
        wo = (int16_t)(w + left + right);
        ho = (int16_t)(h + top + bottom);
    } else {
        // full -> work: shrink inward, carving the decorations back out
        xo = (int16_t)(x + left);
        yo = (int16_t)(y + top);
        wo = (int16_t)(w - left - right);
        ho = (int16_t)(h - top - bottom);
    }

    aes_pb_set_intout(pb, 0, 1);
    aes_pb_set_intout(pb, 1, xo);
    aes_pb_set_intout(pb, 2, yo);
    aes_pb_set_intout(pb, 3, wo);
    aes_pb_set_intout(pb, 4, ho);

    log_write(LOG_API, "wind_calc(%s, kind=0x%04X, %d,%d,%d,%d) -> %d,%d,%d,%d", wc == 0 ? "work->full" : "full->work", (uint16_t)kind, x, y, w, h, xo, yo, wo, ho);
}

// [NO-OP] The semaphore exists to serialize drawing between concurrent applications, and this layer runs exactly one.
void aes_wind_update(const AesPB *pb) {
    int16_t flag = aes_pb_intin(pb, 0);

    aes_pb_set_intout(pb, 0, 1);

    log_write(LOG_API, "wind_update(%d) -> accepted, no concurrent drawing to serialize against", flag);
}

// [FULL] Returns 0 for the desktop when no window covers the point, which is the handle GEM reserves for it.
void aes_wind_find(const AesPB *pb) {
    int16_t x = aes_pb_intin(pb, 0);
    int16_t y = aes_pb_intin(pb, 1);

    int found = window_at(x, y);

    aes_pb_set_intout(pb, 0, (int16_t)found);
    log_write(LOG_API, "wind_find(%d,%d) -> handle=%d", x, y, found);
}

// [PARTIAL] Handles the title, geometry, z-order and slider fields, plus WF_NEWDESK on the desktop handle; any other field is logged and ignored.
void aes_wind_set(const AesPB *pb) {
    int16_t handle = aes_pb_intin(pb, 0);
    int16_t field = aes_pb_intin(pb, 1);

    if (handle == 0) {
        if (field == WF_NEWDESK) {
            s_desktop_tree = aes_pb_intin_long(pb, 2);
            repaint_desktop_area(0, 0, (int16_t)GUEST_SCREEN_W, (int16_t)GUEST_SCREEN_H);
            canvas_present();
            log_write(LOG_API, "wind_set(handle=0, WF_NEWDESK, tree=0x%08X) -> desktop background tree installed", s_desktop_tree);
        } else {
            log_write(LOG_ERROR, "wind_set(handle=0, field=0x%02X): unhandled desktop field", field);
        }
        return;
    }

    AesWindow *win = get_window(handle);
    if (!win) {
        log_write(LOG_ERROR, "wind_set(handle=%d, field=0x%02X): invalid handle", handle, field);
        return;
    }

    switch (field) {
        case WF_NAME:
        case WF_INFO: {
            unsigned int str_addr = aes_pb_intin_long(pb, 2);
            char *dest = (field == WF_NAME) ? win->title : win->info;
            guest_read_cstring(str_addr, dest, sizeof(win->title));
            if (win->opened) {
                draw_window_frame(handle);
                canvas_present();
            }
            log_write(LOG_API, "wind_set(handle=%d, %s, \"%s\") -> redrawn into the window frame", handle, field == WF_NAME ? "WF_NAME" : "WF_INFO", dest);
            break;
        }

        case WF_CURRXYWH: {
            int16_t x = aes_pb_intin(pb, 2);
            int16_t y = aes_pb_intin(pb, 3);
            int16_t w = aes_pb_intin(pb, 4);
            int16_t h = aes_pb_intin(pb, 5);

            int16_t ox = win->full_x, oy = win->full_y, ow = win->full_w, oh = win->full_h;
            win->full_x = x;
            win->full_y = y;
            win->full_w = w;
            win->full_h = h;

            if (win->opened) {
                repaint_desktop_area(ox, oy, ow, oh);
                mark_dirty(ox, oy, ow, oh);
                mark_dirty(x, y, w, h);
                mark_window_dirty(handle);
                redraw_all_frames();
                canvas_present();
            }
            log_write(LOG_API, "wind_set(handle=%d, WF_CURRXYWH, %d,%d,%d,%d) -> moved/resized on the shared screen", handle, x, y, w, h);
            break;
        }

        case WF_TOP: {
            if (z_top_handle() != handle && win->opened) {
                z_raise(handle);
                mark_window_dirty(handle);
                redraw_all_frames();
                canvas_present();
            }
            log_write(LOG_API, "wind_set(handle=%d, WF_TOP) -> raised to the front of %d open window(s)", handle, s_zcount);
            break;
        }

        case WF_HSLIDE:
        case WF_VSLIDE:
        case WF_HSLSIZE:
        case WF_VSLSIZE: {
            int16_t value = aes_pb_intin(pb, 2);
            switch (field) {
                case WF_HSLIDE: win->hslide = value; break;
                case WF_VSLIDE: win->vslide = value; break;
                case WF_HSLSIZE: win->hslsize = value; break;
                default:        win->vslsize = value; break;
            }
            if (win->opened) {
                draw_window_frame(handle);
                canvas_present();
            }
            log_write(LOG_API, "wind_set(handle=%d, field=0x%02X, %d) -> slider redrawn", handle, field, value);
            break;
        }

        default:
            log_write(LOG_ERROR, "wind_set(handle=%d): unhandled field 0x%02X", handle, field);
            break;
    }
}

// DRAW TARGETING

static int owner_of_rect(int16_t x, int16_t y, int16_t w, int16_t h) {
    int contains_work = 0, contains_full = 0;

    for (int i = 0; i < s_zcount; i++) {
        int handle = s_zorder[i];
        AesWindow *win = get_window((int16_t)handle);
        if (!win || !win->opened) {
            continue;
        }

        int16_t work[4], full[4];
        window_rect(win, 1, work);
        window_rect(win, 0, full);

        if ((work[0] == x && work[1] == y && work[2] == w && work[3] == h) || (full[0] == x && full[1] == y && full[2] == w && full[3] == h)) {
            return handle; // exact match wins outright
        }
        if (!contains_work && rect_contains(work, x, y, w, h)) {
            contains_work = handle;
        }
        if (!contains_full && rect_contains(full, x, y, w, h)) {
            contains_full = handle;
        }
    }

    if (contains_work) return contains_work;
    if (contains_full) return contains_full;
    return 0; // desktop
}

// CLIPPING & OVERLAYS

void aes_window_begin_overlay(void) {
    s_overlay_depth++;
}

int aes_window_end_overlay(void) {
    if (s_overlay_depth > 0) {
        s_overlay_depth--;
    }
    return s_overlay_depth;
}

void aes_window_overlay_finished(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    repaint_desktop_area(x, y, w, h);
    mark_dirty(x, y, w, h);
    redraw_all_frames();
    canvas_present();
}

int aes_window_set_clip(int16_t x, int16_t y, int16_t w, int16_t h) {
    canvas_ensure();
    if (!canvas_ready() || w <= 0 || h <= 0) {
        s_active_handle = -1;
        return 0;
    }

    s_active_handle = s_clip_to_desktop ? 0 : (s_overlay_depth > 0 ? OWNER_MENU : owner_of_rect(x, y, w, h));

    canvas_select_clip(visible_region(s_active_handle, x, y, w, h));

    s_last_clip_rect[0] = x;
    s_last_clip_rect[1] = y;
    s_last_clip_rect[2] = w;
    s_last_clip_rect[3] = h;
    return 1;
}

void aes_window_clear_clip(void) {
    canvas_select_clip(NULL);
    canvas_present();
    s_active_handle = -1;
}

static int s_save_active_handle = -1;
static int16_t s_save_clip_rect[4];

void aes_window_save_clip(void) {
    s_save_active_handle = s_active_handle;
    s_save_clip_rect[0] = s_last_clip_rect[0];
    s_save_clip_rect[1] = s_last_clip_rect[1];
    s_save_clip_rect[2] = s_last_clip_rect[2];
    s_save_clip_rect[3] = s_last_clip_rect[3];
}

void aes_window_restore_clip(void) {
    if (s_save_active_handle < 0) {
        return; // nothing was active before; leave it cleared
    }
    aes_window_set_clip(s_save_clip_rect[0], s_save_clip_rect[1], s_save_clip_rect[2], s_save_clip_rect[3]);
}