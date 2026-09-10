#ifndef SCREEN_CANVAS_H
#define SCREEN_CANVAS_H

#include <stdint.h>
#include <windows.h>
#include "screen.h"

#define CANVAS_CHAR_W 8
#define CANVAS_CHAR_H 16
#define CANVAS_SMALL_CHAR_W 6
#define CANVAS_SMALL_CHAR_H 6

typedef struct {
    // Returns non-zero if the rect resolved to something drawable. NULL = clip to the plain rect.
    int  (*set_clip)(int16_t x, int16_t y, int16_t w, int16_t h);
    void (*clear_clip)(void);
    void (*click)(int16_t x, int16_t y);          // left button pressed, in guest coordinates
    void (*key)(unsigned int scan, unsigned int ascii);
    void (*created)(void);                        // canvas exists; paint whatever the screen starts as
} CanvasHooks;

void canvas_set_hooks(const CanvasHooks *hooks);

// LIFECYCLE

void canvas_ensure(void); // creates the host window and bitmap on first use
int  canvas_ready(void);
void canvas_present(void); // blit the canvas to the host window now
HWND canvas_hwnd(void);    // for the drag outline, which paints on the host window rather than the canvas

void canvas_pump_messages(void);

// COORDINATES

void canvas_real_to_guest(int *x, int *y);
void canvas_cursor_pos(int *x, int *y);

// CLIPPING
// The region forms take ownership of the HRGN. canvas_set_clip() routes through the installed policy.

void canvas_select_clip(HRGN rgn); // NULL clears
void canvas_intersect_clip(HRGN rgn);
int  canvas_set_clip(int16_t x, int16_t y, int16_t w, int16_t h);
void canvas_clear_clip(void);

// PRIMITIVES

void canvas_fill(int16_t x, int16_t y, int16_t w, int16_t h, unsigned int color_rgb);
void canvas_invert(int16_t x, int16_t y, int16_t w, int16_t h);
void canvas_text(int16_t x, int16_t y, const char *text, unsigned int color_rgb);
void canvas_text_font(int16_t x, int16_t y, const char *text, unsigned int color_rgb, int small_font);

#define CANVAS_VALIGN_BASELINE 0
#define CANVAS_VALIGN_HALF     1
#define CANVAS_VALIGN_ASCENT   2
#define CANVAS_VALIGN_BOTTOM   3
#define CANVAS_VALIGN_DESCENT  4
#define CANVAS_VALIGN_TOP      5

#define CANVAS_HALIGN_LEFT   0
#define CANVAS_HALIGN_CENTER 1
#define CANVAS_HALIGN_RIGHT  2

// Styled text in the system font; in opaque mode it paints the character cell background too.
void canvas_styled_text(int16_t x, int16_t y, const char *text, unsigned int color_rgb, int bold, int italic, int underline, int opaque, unsigned int bg_rgb, int halign, int valign);

void canvas_line(int16_t x1, int16_t y1, int16_t x2, int16_t y2, unsigned int color_rgb, int xor_mode);
void canvas_blit(int16_t src_x, int16_t src_y, int16_t dst_x, int16_t dst_y, int16_t w, int16_t h, unsigned int rop);

// AREA SNAPSHOTS
// Copy of a screen area, so an overlay can put back exactly what it covered.

void *canvas_save_area(int16_t x, int16_t y, int16_t w, int16_t h);
void canvas_restore_area(void *snapshot);

#endif