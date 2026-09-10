#include <windows.h>
#include "aes/aes_graf.h"
#include "aes/aes_window.h"
#include "bios/bios_kbd.h"
#include "logger.h"
#include "tos_layer.h"

#define AES_GRAF_VDI_HANDLE 1
#define AES_GRAF_BUTTON_LEFT 0x01

// AES ENTRY POINTS

// [FULL] The cell metrics are fixed because the canvas has exactly one font; they must agree with vqt_fontinfo().
void aes_graf_handle(const AesPB *pb) {
    aes_pb_set_intout(pb, 0, AES_GRAF_VDI_HANDLE);
    aes_pb_set_intout(pb, 1, 8);  // character cell width
    aes_pb_set_intout(pb, 2, 16); // character cell height
    aes_pb_set_intout(pb, 3, 8);  // BOXCHAR bounding box width
    aes_pb_set_intout(pb, 4, 16); // BOXCHAR bounding box height

    log_write(LOG_API, "graf_handle() -> vdi_handle=%d, charw=8, charh=16", AES_GRAF_VDI_HANDLE);
}

// [FULL] A non-blocking poll, since callers use it to sample the mouse continuously while tracking a drag.
void aes_graf_mkstate(const AesPB *pb) {
    Sleep(1); // A real 68k was throttled by its own clock speed; without this the polling loop spins orders of magnitude faster than the guest expects.

    int gx, gy;
    aes_window_cursor_pos(&gx, &gy);
    int16_t mx = (int16_t)gx;
    int16_t my = (int16_t)gy;
    int16_t mb = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? AES_GRAF_BUTTON_LEFT : 0;

    aes_pb_set_intout(pb, 0, 1); // reserved, always 1
    aes_pb_set_intout(pb, 1, mx);
    aes_pb_set_intout(pb, 2, my);
    aes_pb_set_intout(pb, 3, mb);
    aes_pb_set_intout(pb, 4, bios_kbd_shift_state());

    log_write(LOG_API, "graf_mkstate() -> mx=%d, my=%d, button=%d", mx, my, mb);
}

#define AES_M_OFF            256
#define AES_M_ON             257
#define AES_POINTER_ARROW     0
#define AES_POINTER_TEXT      1
#define AES_POINTER_BUSY      2
#define AES_POINTER_POINT     3
#define AES_POINTER_FLAT      4
#define AES_POINTER_THIN_X    5
#define AES_POINTER_THICK_X   6
#define AES_POINTER_OUTLN_X   7

// [PARTIAL] Shape changes map onto host cursors, but M_OFF and M_ON are ignored: a guest that hides the pointer for long stretches leaves the host user without one.
void aes_graf_mouse(const AesPB *pb) {
    int16_t mode = aes_pb_intin(pb, 0);
    if (mode == AES_M_OFF) {
        // ShowCursor(FALSE);
        log_write(LOG_API, "graf_mouse(M_OFF) -> accepted, cursor left visible (hiding disabled)");
        return;
    }
    if (mode == AES_M_ON) {
        // ShowCursor(TRUE);
        log_write(LOG_API, "graf_mouse(M_ON) -> accepted, cursor was never hidden");
        return;
    }

    LPCSTR cursor_id = IDC_ARROW;
    const char *cursor_name = "IDC_ARROW";
    switch (mode) {
        case AES_POINTER_TEXT:
            cursor_id = IDC_IBEAM; cursor_name = "IDC_IBEAM"; break;
        case AES_POINTER_BUSY:
            cursor_id = IDC_WAIT; cursor_name = "IDC_WAIT"; break;
        case AES_POINTER_POINT:
        case AES_POINTER_FLAT:
            cursor_id = IDC_HAND; cursor_name = "IDC_HAND"; break;
        case AES_POINTER_THIN_X:
        case AES_POINTER_THICK_X:
        case AES_POINTER_OUTLN_X:
            cursor_id = IDC_CROSS; cursor_name = "IDC_CROSS"; break;
        case AES_POINTER_ARROW:
        default:
            cursor_id = IDC_ARROW; cursor_name = "IDC_ARROW"; break;
    }

    SetCursor(LoadCursorA(NULL, cursor_id));
    log_write(LOG_API, "graf_mouse(%d) -> SetCursor(%s)", mode, cursor_name);
}

// A hollow 1px XOR outline; drawing it twice at the same rect draws then erases it.
static void queue_box_outline(int16_t x, int16_t y, int16_t w, int16_t h) {
    if (w <= 0 || h <= 0) {
        return;
    }
    canvas_invert(x, y, w, 1);
    canvas_invert(x, (int16_t)(y + h - 1), w, 1);
    canvas_invert(x, y, 1, h);
    canvas_invert((int16_t)(x + w - 1), y, 1, h);
}

// [FULL] Blocks until the button comes up, which is what the caller expects; the loop pumps host messages so the window stays responsive meanwhile.
void aes_graf_rubberbox(const AesPB *pb) {
    int16_t x = aes_pb_intin(pb, 0);
    int16_t y = aes_pb_intin(pb, 1);
    int16_t min_w = aes_pb_intin(pb, 2);
    int16_t min_h = aes_pb_intin(pb, 3);
    if (min_w < 1) min_w = 1;
    if (min_h < 1) min_h = 1;

    int16_t w = min_w, h = min_h;
    int have_prev = 0;
    int16_t prev_w = 0, prev_h = 0;

    for (;;) {
        canvas_pump_messages();
        if (tos_is_halted()) {
            break;
        }

        int gx, gy;
        aes_window_cursor_pos(&gx, &gy);
        int16_t nw = (int16_t)(gx - x + 1);
        int16_t nh = (int16_t)(gy - y + 1);
        if (nw < min_w) nw = min_w;
        if (nh < min_h) nh = min_h;

        if (!have_prev || nw != prev_w || nh != prev_h) {
            if (have_prev) {
                queue_box_outline(x, y, prev_w, prev_h);
            }
            queue_box_outline(x, y, nw, nh);
            w = nw;
            h = nh;
            prev_w = nw;
            prev_h = nh;
            have_prev = 1;
        }

        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            break;
        }
        Sleep(10);
    }

    if (have_prev) {
        queue_box_outline(x, y, prev_w, prev_h); // erase the last frame; the caller does its own real drawing next
    }

    aes_pb_set_intout(pb, 0, w);
    aes_pb_set_intout(pb, 1, h);

    log_write(LOG_API, "graf_rubberbox(%d,%d, min=%d,%d) -> %d,%d", x, y, min_w, min_h, w, h);
}

#define GROWBOX_FRAMES 8
#define GROWBOX_FRAME_MS 15

// [FULL] Only animates; the caller's own wind_open or objc_draw is what actually leaves something on screen afterwards.
void aes_graf_growbox(const AesPB *pb) {
    int16_t x1 = aes_pb_intin(pb, 0);
    int16_t y1 = aes_pb_intin(pb, 1);
    int16_t w1 = aes_pb_intin(pb, 2);
    int16_t h1 = aes_pb_intin(pb, 3);
    int16_t x2 = aes_pb_intin(pb, 4);
    int16_t y2 = aes_pb_intin(pb, 5);
    int16_t w2 = aes_pb_intin(pb, 6);
    int16_t h2 = aes_pb_intin(pb, 7);

    for (int i = 0; i <= GROWBOX_FRAMES; i++) {
        int16_t x = (int16_t)(x1 + (x2 - x1) * i / GROWBOX_FRAMES);
        int16_t y = (int16_t)(y1 + (y2 - y1) * i / GROWBOX_FRAMES);
        int16_t w = (int16_t)(w1 + (w2 - w1) * i / GROWBOX_FRAMES);
        int16_t h = (int16_t)(h1 + (h2 - h1) * i / GROWBOX_FRAMES);

        canvas_pump_messages();
        if (tos_is_halted()) {
            return;
        }

        queue_box_outline(x, y, w, h);
        Sleep(GROWBOX_FRAME_MS);
        queue_box_outline(x, y, w, h); // erase before the next frame draws
    }

    aes_pb_set_intout(pb, 0, 1);
    log_write(LOG_API, "graf_growbox(%d,%d,%d,%d -> %d,%d,%d,%d) -> animated", x1, y1, w1, h1, x2, y2, w2, h2);
}