#ifndef AES_WINDOW_H
#define AES_WINDOW_H

#include <stdint.h>
#include "aes/aes_pb.h"
#include "screen_canvas.h"

// The AES's view of the system font cell; the canvas owns the actual metrics.
#define AES_CHAR_W       CANVAS_CHAR_W
#define AES_CHAR_H       CANVAS_CHAR_H
#define AES_SMALL_CHAR_W CANVAS_SMALL_CHAR_W
#define AES_SMALL_CHAR_H CANVAS_SMALL_CHAR_H

void aes_window_init(void);

void aes_wind_create(const AesPB *pb);
void aes_wind_open(const AesPB *pb);
void aes_wind_close(const AesPB *pb);
void aes_wind_delete(const AesPB *pb);
void aes_wind_get(const AesPB *pb);
void aes_wind_set(const AesPB *pb);
void aes_wind_calc(const AesPB *pb);
void aes_wind_update(const AesPB *pb);
void aes_wind_find(const AesPB *pb);

#define AES_MN_SELECTED 10
#define AES_WM_REDRAW   20
#define AES_WM_TOPPED   21
#define AES_WM_CLOSED   22
#define AES_WM_FULLED   23
#define AES_WM_ARROWED  24
#define AES_WM_HSLID    25
#define AES_WM_VSLID    26
#define AES_WM_SIZED    27
#define AES_WM_MOVED    28
#define AES_SCAN_UP     0x48
#define AES_SCAN_LEFT   0x4B
#define AES_SCAN_RIGHT  0x4D
#define AES_SCAN_DOWN   0x50
#define AES_SCAN_DELETE 0x53

int aes_window_poll_key(unsigned int *out_code);
int aes_window_pump(int *out_code, int *out_handle, int16_t rect[4]);
void aes_window_queue_message(int code, int handle, int16_t a, int16_t b, int16_t c, int16_t d);

// Real cursor position translated into logical guest coordinates. Every AES entry point that samples the mouse outside an event message goes through this.
void aes_window_cursor_pos(int *x, int *y);

// The menu bar owns a strip along the top of the shared screen that windows are never allowed to paint into and that they never clip.
void aes_window_set_menu_height(int16_t h);
int16_t aes_window_menu_height(void);

int aes_window_set_clip(int16_t x, int16_t y, int16_t w, int16_t h);
void aes_window_clear_clip(void);
void aes_window_save_clip(void);
void aes_window_restore_clip(void);

// While an overlay is open, drawing is clipped only to its own rect instead of being cut down to whichever window owns that part of the screen; this is what lets menus and modal dialogs paint over windows.
void aes_window_begin_overlay(void);
int aes_window_end_overlay(void); // returns the depth remaining after this call

// Puts back what an overlay covered: repaints the desktop under the rect and queues WM_REDRAW for every window it overlapped.
void aes_window_overlay_finished(int16_t x, int16_t y, int16_t w, int16_t h);

#endif