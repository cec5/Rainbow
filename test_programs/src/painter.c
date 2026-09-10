/* Lightweight TOS Paint Program: Canvas with a toolbar of 7 rainbow color
 * swatches, small/large brush buttons, and a clear button. Press and hold
 * the left mouse button over the canvas to draw; release to stop. */
#include <gem.h>
#include <tos.h>

#define TOOLBAR_H    100
#define SWATCH_SIZE  40
#define SWATCH_GAP   6
#define NUM_COLORS   7
#define BTN_W        70
#define BTN_H        46
#define BTN_GAP      8

#define BRUSH_SMALL  2
#define BRUSH_LARGE  6

#define COLOR_RED     9
#define COLOR_ORANGE  10
#define COLOR_YELLOW  11
#define COLOR_GREEN   12
#define COLOR_BLUE    13
#define COLOR_INDIGO  14
#define COLOR_VIOLET  15
#define COLOR_BG      0
#define COLOR_INK     1
#define COLOR_PANEL   8

static short vdi_handle;
static short win_handle;
static short canvas_x, canvas_y, canvas_w, canvas_h;
static short toolbar_y;
static short swatch_x[NUM_COLORS];
static short small_btn_x, large_btn_x, clear_btn_x;
static short current_color = COLOR_RED;
static short brush_size = BRUSH_SMALL;

static short win_fulled = 0;
static short win_prev[4];

#define MAX_DABS 16384
typedef struct { short rel_x, rel_y, color, size; } DabRecord;
static DabRecord dab_history[MAX_DABS];
static int dab_count = 0;

static void set_palette_color(short index, short r, short g, short b) {
    short rgb[3];
    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
    vs_color(vdi_handle, index, rgb);
}

static void set_rainbow_palette(void) {
    set_palette_color(COLOR_RED,    1000, 0,    0);
    set_palette_color(COLOR_ORANGE, 1000, 500,  0);
    set_palette_color(COLOR_YELLOW, 1000, 1000, 0);
    set_palette_color(COLOR_GREEN,  0,    1000, 0);
    set_palette_color(COLOR_BLUE,   0,    0,    1000);
    set_palette_color(COLOR_INDIGO, 290,  0,    510);
    set_palette_color(COLOR_VIOLET, 930,  510,  930);
}

static void layout(void) {
    short x, i;

    wind_get(win_handle, WF_WORKXYWH, &canvas_x, &canvas_y, &canvas_w, &canvas_h);
    toolbar_y = canvas_y + canvas_h - TOOLBAR_H;

    x = canvas_x + 8;
    for (i = 0; i < NUM_COLORS; i++) {
        swatch_x[i] = x;
        x += SWATCH_SIZE + SWATCH_GAP;
    }

    x += 8;
    small_btn_x = x;
    x += BTN_W + BTN_GAP;
    large_btn_x = x;
    x += BTN_W + BTN_GAP + 8;
    clear_btn_x = x;
}

static void draw_button(short x, const char *label, int selected) {
    short r[4];
    short y = toolbar_y + (TOOLBAR_H - BTN_H) / 2;

    r[0] = x;
    r[1] = y;
    r[2] = x + BTN_W - 1;
    r[3] = y + BTN_H - 1;

    vsf_color(vdi_handle, selected ? COLOR_INK : COLOR_BG);
    v_bar(vdi_handle, r);

    vst_color(vdi_handle, selected ? COLOR_BG : COLOR_INK);
    vswr_mode(vdi_handle, MD_TRANS);
    v_gtext(vdi_handle, x + 6, y + BTN_H / 2 - 8, label);
    vswr_mode(vdi_handle, MD_REPLACE);
}

static void draw_toolbar(void) {
    short i;
    short y = toolbar_y + (TOOLBAR_H - SWATCH_SIZE) / 2;
    short r[4];

    r[0] = canvas_x;
    r[1] = toolbar_y;
    r[2] = canvas_x + canvas_w - 1;
    r[3] = toolbar_y + TOOLBAR_H - 1;
    vsf_color(vdi_handle, COLOR_PANEL);
    v_bar(vdi_handle, r);

    for (i = 0; i < NUM_COLORS; i++) {
        short cx1 = swatch_x[i];
        short cy1 = y;
        short cx2 = swatch_x[i] + SWATCH_SIZE - 1;
        short cy2 = y + SWATCH_SIZE - 1;

        if (COLOR_RED + i == current_color) {
            r[0] = cx1 - 2;
            r[1] = cy1 - 2;
            r[2] = cx2 + 2;
            r[3] = cy2 + 2;
            vsf_color(vdi_handle, COLOR_INK);
            v_bar(vdi_handle, r);
        }

        r[0] = cx1;
        r[1] = cy1;
        r[2] = cx2;
        r[3] = cy2;
        vsf_color(vdi_handle, COLOR_RED + i);
        v_bar(vdi_handle, r);
    }

    draw_button(small_btn_x, "Small", brush_size == BRUSH_SMALL);
    draw_button(large_btn_x, "Large", brush_size == BRUSH_LARGE);
    draw_button(clear_btn_x, "Clear", 0);
}

static void clear_canvas(void) {
    short r[4];
    r[0] = canvas_x;
    r[1] = canvas_y;
    r[2] = canvas_x + canvas_w - 1;
    r[3] = toolbar_y - 1;
    vsf_color(vdi_handle, COLOR_BG);
    v_bar(vdi_handle, r);
}

static void replay_strokes(void) {
    int i;
    short clip[4];
    clip[0] = canvas_x;
    clip[1] = canvas_y;
    clip[2] = canvas_x + canvas_w - 1;
    clip[3] = toolbar_y - 1;
    vs_clip(vdi_handle, 1, clip);

    for (i = 0; i < dab_count; i++) {
        short r[4];
        short x = canvas_x + dab_history[i].rel_x;
        short y = canvas_y + dab_history[i].rel_y;
        r[0] = x - dab_history[i].size;
        r[1] = y - dab_history[i].size;
        r[2] = x + dab_history[i].size;
        r[3] = y + dab_history[i].size;
        vsf_color(vdi_handle, dab_history[i].color);
        v_bar(vdi_handle, r);
    }

    vs_clip(vdi_handle, 0, 0);
}

static void redraw_all(void) {
    short clip[4];
    clip[0] = canvas_x;
    clip[1] = canvas_y;
    clip[2] = canvas_x + canvas_w - 1;
    clip[3] = canvas_y + canvas_h - 1;
    vs_clip(vdi_handle, 1, clip);

    clear_canvas();
    draw_toolbar();

    vs_clip(vdi_handle, 0, 0);

    replay_strokes();
}

static int in_rect(short x, short y, short rx, short ry, short rw, short rh) {
    return x >= rx && y >= ry && x < rx + rw && y < ry + rh;
}

static void dab(short x, short y) {
    short r[4];
    short clip[4];

    if (dab_count < MAX_DABS) {
        dab_history[dab_count].rel_x = x - canvas_x;
        dab_history[dab_count].rel_y = y - canvas_y;
        dab_history[dab_count].color = current_color;
        dab_history[dab_count].size = brush_size;
        dab_count++;
    }

    r[0] = x - brush_size;
    r[1] = y - brush_size;
    r[2] = x + brush_size;
    r[3] = y + brush_size;

    clip[0] = canvas_x;
    clip[1] = canvas_y;
    clip[2] = canvas_x + canvas_w - 1;
    clip[3] = toolbar_y - 1;
    vs_clip(vdi_handle, 1, clip);

    vsf_color(vdi_handle, current_color);
    v_bar(vdi_handle, r);

    vs_clip(vdi_handle, 0, 0);
}

static void clip_toolbar(void) {
    short clip[4];
    clip[0] = canvas_x;
    clip[1] = toolbar_y;
    clip[2] = canvas_x + canvas_w - 1;
    clip[3] = toolbar_y + TOOLBAR_H - 1;
    vs_clip(vdi_handle, 1, clip);
}

static void handle_toolbar_click(short mx, short my) {
    short i;
    short swatch_y = toolbar_y + (TOOLBAR_H - SWATCH_SIZE) / 2;

    for (i = 0; i < NUM_COLORS; i++) {
        if (in_rect(mx, my, swatch_x[i], swatch_y, SWATCH_SIZE, SWATCH_SIZE)) {
            current_color = COLOR_RED + i;
            clip_toolbar();
            draw_toolbar();
            vs_clip(vdi_handle, 0, 0);
            return;
        }
    }

    if (in_rect(mx, my, small_btn_x, toolbar_y, BTN_W, TOOLBAR_H)) {
        brush_size = BRUSH_SMALL;
        clip_toolbar();
        draw_toolbar();
        vs_clip(vdi_handle, 0, 0);
        return;
    }
    if (in_rect(mx, my, large_btn_x, toolbar_y, BTN_W, TOOLBAR_H)) {
        brush_size = BRUSH_LARGE;
        clip_toolbar();
        draw_toolbar();
        vs_clip(vdi_handle, 0, 0);
        return;
    }
    if (in_rect(mx, my, clear_btn_x, toolbar_y, BTN_W, TOOLBAR_H)) {
        short clip[4];
        clip[0] = canvas_x;
        clip[1] = canvas_y;
        clip[2] = canvas_x + canvas_w - 1;
        clip[3] = toolbar_y - 1;
        vs_clip(vdi_handle, 1, clip);
        clear_canvas();
        vs_clip(vdi_handle, 0, 0);
        dab_count = 0;
        return;
    }
}

// Toggles between the window's last windowed rect and the full GEM desktop.
static void toggle_full(void) {
    short x, y, w, h;

    if (win_fulled) {
        wind_set(win_handle, WF_CURRXYWH, win_prev[0], win_prev[1], win_prev[2], win_prev[3]);
        win_fulled = 0;
        return;
    }

    wind_get(win_handle, WF_CURRXYWH, &x, &y, &w, &h);
    win_prev[0] = x;
    win_prev[1] = y;
    win_prev[2] = w;
    win_prev[3] = h;

    wind_get(win_handle, WF_FULLXYWH, &x, &y, &w, &h);
    wind_set(win_handle, WF_CURRXYWH, x, y, w, h);
    win_fulled = 1;
}

// Polls the mouse directly while the button is held, rather than going back through evnt_multi for every sample.
static void track_stroke(short mx, short my) {
    short px = -1, py = -1;
    short bstate, kstate;

    while (1) {
        graf_mkstate(&mx, &my, &bstate, &kstate);
        if (!(bstate & 0x01)) {
            break;
        }
        if (mx != px || my != py) {
            dab(mx, my);
            px = mx;
            py = my;
        }
    }
}

int main(void) {
    short ap_id = appl_init();
    if (ap_id < 0) {
        return 1;
    }

    short charw, charh, boxw, boxh;
    short phys_handle = graf_handle(&charw, &charh, &boxw, &boxh);

    short work_in[11];
    short i;
    for (i = 0; i < 10; i++) {
        work_in[i] = 1;
    }
    work_in[10] = 2;

    vdi_handle = phys_handle;
    short work_out[57];
    v_opnvwk(work_in, &vdi_handle, work_out);
    if (vdi_handle == 0) {
        appl_exit();
        return 1;
    }

    vsf_interior(vdi_handle, FIS_SOLID);

    set_rainbow_palette();

    short desk_x, desk_y, desk_w, desk_h;
    wind_get(0, WF_WORKXYWH, &desk_x, &desk_y, &desk_w, &desk_h);

    short parts = NAME | MOVER | CLOSER | SIZER | FULLER;
    win_handle = wind_create(parts, desk_x, desk_y, desk_w, desk_h);
    if (win_handle < 0) {
        v_clsvwk(vdi_handle);
        appl_exit();
        return 1;
    }

    wind_set_str(win_handle, WF_NAME, "Chris's Atari Painter");
    // Open windowed rather than already filling the desktop, so FULLER has a windowed state to toggle back to.
    wind_open(win_handle, desk_x + 20, desk_y + 20, desk_w - 40, desk_h - 40);

    int running = 1;
    while (running) {
        short msg[8];
        short mx, my, mb, ks, kc, mc;
        short events = evnt_multi(MU_BUTTON | MU_MESAG, 1, 0x01, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, msg, 0, &mx, &my, &mb, &ks, &kc, &mc);

        if (events & MU_MESAG) {
            switch (msg[0]) {
                case WM_REDRAW:
                    layout();
                    redraw_all();
                    break;
                case WM_MOVED:
                case WM_SIZED:
                    win_fulled = 0;
                    wind_set(win_handle, WF_CURRXYWH, msg[4], msg[5], msg[6], msg[7]);
                    break;
                case WM_FULLED:
                    toggle_full();
                    break;
                case WM_CLOSED:
                    running = 0;
                    break;
                default:
                    break;
            }
        }

        if (running && (events & MU_BUTTON) && (mb & 0x01)) {
            if (my >= toolbar_y) {
                handle_toolbar_click(mx, my);
            } else if (in_rect(mx, my, canvas_x, canvas_y, canvas_w, canvas_h - TOOLBAR_H)) {
                dab(mx, my);
                track_stroke(mx, my);
            }
        }
    }
    v_clsvwk(vdi_handle);
    wind_close(win_handle);
    wind_delete(win_handle);
    appl_exit();
    return 0;
}