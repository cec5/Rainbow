/* Two deliberately overlapping windows, to exercise multi-window occlusion,
 * z-order, and the AES rule that window chrome only reports what the user did.
 * Moving, sizing and fulling are the application's job to carry out. */

#include <gem.h>
#include <tos.h>

#define NUM_WINDOWS 2

typedef struct {
    short handle;
    short fulled;
    short prev[4];
} Window;

static short vdi_handle;
static Window windows[NUM_WINDOWS];

static Window *window_for(short handle) {
    short i;
    for (i = 0; i < NUM_WINDOWS; i++) {
        if (windows[i].handle == handle) {
            return &windows[i];
        }
    }
    return 0;
}

static void draw_window(short handle, short fill_colour, const char *label, short dx, short dy, short dw, short dh) {
    short rx, ry, rw, rh;
    short wx, wy, ww, wh;

    wind_get(handle, WF_WORKXYWH, &wx, &wy, &ww, &wh);

    wind_get(handle, WF_FIRSTXYWH, &rx, &ry, &rw, &rh);
    while (rw > 0 && rh > 0) {
        short cx = rx > dx ? rx : dx;
        short cy = ry > dy ? ry : dy;
        short cr = (rx + rw < dx + dw) ? (rx + rw) : (dx + dw);
        short cb = (ry + rh < dy + dh) ? (ry + rh) : (dy + dh);

        if (cr > cx && cb > cy) {
            short clip[4];
            short y;

            clip[0] = cx;
            clip[1] = cy;
            clip[2] = cr - 1;
            clip[3] = cb - 1;
            vs_clip(vdi_handle, 1, clip);

            vsf_color(vdi_handle, fill_colour);
            v_bar(vdi_handle, clip);

            // Stripes make it obvious if a covered window bleeds through
            vsf_color(vdi_handle, 1);
            for (y = wy; y < wy + wh; y += 16) {
                short bar[4];
                bar[0] = wx;
                bar[1] = y;
                bar[2] = wx + ww - 1;
                bar[3] = y + 1;
                v_bar(vdi_handle, bar);
            }

            vst_color(vdi_handle, 1);
            vswr_mode(vdi_handle, MD_TRANS);
            v_gtext(vdi_handle, wx + 8, wy + 24, (char *)label);
            vswr_mode(vdi_handle, MD_REPLACE);

            vs_clip(vdi_handle, 0, 0);
        }

        wind_get(handle, WF_NEXTXYWH, &rx, &ry, &rw, &rh);
    }
}

static void apply_rect(short handle, const short *msg) {
    Window *w = window_for(handle);
    if (w) {
        w->fulled = 0;
    }
    wind_set(handle, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
}

static void toggle_full(short handle) {
    Window *w = window_for(handle);
    short x, y, wd, ht;

    if (!w) {
        return;
    }

    if (w->fulled) {
        wind_set(handle, WF_CURRXYWH, w->prev[0], w->prev[1], w->prev[2], w->prev[3]);
        w->fulled = 0;
        return;
    }

    wind_get(handle, WF_CURRXYWH, &x, &y, &wd, &ht);
    w->prev[0] = x;
    w->prev[1] = y;
    w->prev[2] = wd;
    w->prev[3] = ht;

    wind_get(handle, WF_FULLXYWH, &x, &y, &wd, &ht);
    wind_set(handle, WF_CURRXYWH, x, y, wd, ht);
    w->fulled = 1;
}

int main(void) {
    short ap_id = appl_init();
    short charw, charh, boxw, boxh;
    short phys_handle;
    short work_in[11];
    short work_out[57];
    short i;
    short desk_x, desk_y, desk_w, desk_h;
    short parts;
    short win_a, win_b;
    short open_windows;

    if (ap_id < 0) {
        return 1;
    }

    phys_handle = graf_handle(&charw, &charh, &boxw, &boxh);

    for (i = 0; i < 10; i++) {
        work_in[i] = 1;
    }
    work_in[10] = 2; // raster coordinates

    vdi_handle = phys_handle;
    v_opnvwk(work_in, &vdi_handle, work_out);
    if (vdi_handle == 0) {
        appl_exit();
        return 1;
    }

    vsf_interior(vdi_handle, FIS_SOLID);

    wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);

    parts = NAME | MOVER | CLOSER | FULLER | SIZER;

    win_a = wind_create(parts, desk_x, desk_y, desk_w, desk_h);
    win_b = wind_create(parts, desk_x, desk_y, desk_w, desk_h);
    if (win_a < 0 || win_b < 0) {
        v_clsvwk(vdi_handle);
        appl_exit();
        return 1;
    }

    windows[0].handle = win_a;
    windows[0].fulled = 0;
    windows[1].handle = win_b;
    windows[1].fulled = 0;

    wind_set_str(win_a, WF_NAME, "Window A");
    wind_set_str(win_b, WF_NAME, "Window B");

    wind_open(win_a, desk_x + 20, desk_y + 20, 320, 220);
    wind_open(win_b, desk_x + 180, desk_y + 120, 320, 220);
    open_windows = 2;

    Cconws("Two overlapping windows open. Close both to exit.\r\n");

    while (open_windows > 0) {
        short msg[8];
        evnt_mesag(msg);

        switch (msg[0]) {
            case WM_REDRAW: {
                short colour = (msg[3] == win_a) ? 2 : 3;
                const char *label = (msg[3] == win_a) ? "WINDOW A" : "WINDOW B";
                draw_window(msg[3], colour, label, msg[4], msg[5], msg[6], msg[7]);
                break;
            }

            case WM_TOPPED:
                wind_set(msg[3], WF_TOP, 0, 0, 0, 0);
                break;

            case WM_MOVED:
            case WM_SIZED:
                apply_rect(msg[3], msg);
                break;

            case WM_FULLED:
                toggle_full(msg[3]);
                break;

            case WM_CLOSED:
                wind_close(msg[3]);
                wind_delete(msg[3]);
                open_windows--;
                Cconws("Window closed.\r\n");
                break;

            default:
                break;
        }
    }

    v_clsvwk(vdi_handle);
    appl_exit();
    return 0;
}