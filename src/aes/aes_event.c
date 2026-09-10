#include <stdint.h>
#include <windows.h>
#include "m68k.h"
#include "aes/aes_event.h"
#include "aes/aes_window.h"
#include "bios/bios_kbd.h"
#include "logger.h"
#include "tos_layer.h"

#define MU_KEYBD  0x01
#define MU_BUTTON 0x02
#define MU_MESAG  0x10
#define MU_TIMER  0x20

// HELPERS

// Shared by evnt_button() and evnt_multi(), so an edge consumed by one is not seen by the other. Two callers alternating between them can miss a transition.
static int s_prev_down = 0;

static int mouse_button_edge(int16_t bstate) {
    int down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;
    int edge = (down != s_prev_down) && (down == (bstate ? 1 : 0));
    s_prev_down = down;
    return edge;
}

static unsigned int read_timer_ms(const AesPB *pb) {
    uint16_t low = (uint16_t)aes_pb_intin(pb, 14);
    uint16_t high = (uint16_t)aes_pb_intin(pb, 15);
    return ((unsigned int)high << 16) | low;
}

// AES ENTRY POINTS

static void evnt_wait(const AesPB *pb, const char *name, int support_button) {
    unsigned int msg_buf_addr = aes_pb_addrin(pb, 0);
    int16_t events = support_button ? aes_pb_intin(pb, 0) : 0;
    int16_t bstate = support_button ? aes_pb_intin(pb, 3) : 0;
    int want_button = support_button && (events & MU_BUTTON);
    int want_keybd = support_button && (events & MU_KEYBD);

    unsigned int timer_ms = support_button ? read_timer_ms(pb) : 0xFFFFFFFF;
    int want_timer = support_button && (events & MU_TIMER) && timer_ms != 0xFFFFFFFF;
    DWORD deadline = want_timer ? GetTickCount() + timer_ms : 0;

    int code = 0;
    int handle = 0;
    int16_t rect[4] = {0, 0, 0, 0};

    for (;;) {
        if (tos_is_halted()) {
            return;
        }

        if (aes_window_pump(&code, &handle, rect)) {
            // msg[0]=type, msg[1]=sender ap_id, msg[2]=extra length, msg[3]=handle, msg[4..7]=x,y,w,h of the affected area.
            m68k_write_memory_16(msg_buf_addr + 0, (unsigned int)code);
            m68k_write_memory_16(msg_buf_addr + 2, 1); // single app, always id 1
            m68k_write_memory_16(msg_buf_addr + 4, 0);
            m68k_write_memory_16(msg_buf_addr + 6, (unsigned int)handle);
            m68k_write_memory_16(msg_buf_addr + 8,  (unsigned int)(uint16_t)rect[0]);
            m68k_write_memory_16(msg_buf_addr + 10, (unsigned int)(uint16_t)rect[1]);
            m68k_write_memory_16(msg_buf_addr + 12, (unsigned int)(uint16_t)rect[2]);
            m68k_write_memory_16(msg_buf_addr + 14, (unsigned int)(uint16_t)rect[3]);

            aes_pb_set_intout(pb, 0, MU_MESAG);
            log_write(LOG_API, "%s -> code=%d handle=%d rect=%d,%d,%d,%d", name, code, handle, rect[0], rect[1], rect[2], rect[3]);
            return;
        }

        int edge = mouse_button_edge(bstate);
        if (want_button && edge) {
            int gx, gy;
            aes_window_cursor_pos(&gx, &gy);
            int16_t mx = (int16_t)gx;
            int16_t my = (int16_t)gy;

            aes_pb_set_intout(pb, 0, MU_BUTTON);
            aes_pb_set_intout(pb, 1, mx);
            aes_pb_set_intout(pb, 2, my);
            aes_pb_set_intout(pb, 3, bstate);
            aes_pb_set_intout(pb, 4, bios_kbd_shift_state());
            aes_pb_set_intout(pb, 5, 0);
            aes_pb_set_intout(pb, 6, 0);

            log_write(LOG_API, "%s -> code=MU_BUTTON mx=%d my=%d bstate=%d", name, mx, my, bstate);
            return;
        }

        unsigned int key_code;
        if (want_keybd && aes_window_poll_key(&key_code)) {
            int gx, gy;
            aes_window_cursor_pos(&gx, &gy);
            int16_t kreturn = (int16_t)key_code; // already in GEM's scan-code-high/ASCII-low shape

            aes_pb_set_intout(pb, 0, MU_KEYBD);
            aes_pb_set_intout(pb, 1, (int16_t)gx);
            aes_pb_set_intout(pb, 2, (int16_t)gy);
            aes_pb_set_intout(pb, 3, 0);
            aes_pb_set_intout(pb, 4, bios_kbd_shift_state());
            aes_pb_set_intout(pb, 5, kreturn);
            aes_pb_set_intout(pb, 6, 0);

            log_write(LOG_API, "%s -> code=MU_KEYBD kreturn=0x%04X mx=%d my=%d", name, (unsigned int)(uint16_t)kreturn, gx, gy);
            return;
        }

        if (want_timer && (int)(GetTickCount() - deadline) >= 0) {
            int gx, gy;
            aes_window_cursor_pos(&gx, &gy);

            aes_pb_set_intout(pb, 0, MU_TIMER);
            aes_pb_set_intout(pb, 1, (int16_t)gx);
            aes_pb_set_intout(pb, 2, (int16_t)gy);
            aes_pb_set_intout(pb, 3, 0);
            aes_pb_set_intout(pb, 4, bios_kbd_shift_state());
            aes_pb_set_intout(pb, 5, 0);
            aes_pb_set_intout(pb, 6, 0);

            log_write(LOG_API, "%s -> code=MU_TIMER after %ums", name, timer_ms);
            return;
        }

        Sleep(10);
    }
}

// [FULL] Blocks until a message arrives; nothing else can end the wait.
void aes_evnt_mesag(const AesPB *pb) {
    evnt_wait(pb, "evnt_mesag", 0);
}

// [PARTIAL] Only four of the six mask bits are honored: the two mouse-rectangle events (MU_M1/MU_M2) are never checked. Each call also reports exactly one event, where real GEM may return several bits at once.
void aes_evnt_multi(const AesPB *pb) {
    evnt_wait(pb, "evnt_multi", 1);
}

// [PARTIAL] Only the left button is tracked and multi-click counting is not modeled, so the first matching transition ends the wait and the count always reports one.
void aes_evnt_button(const AesPB *pb) {
    int16_t clicks = aes_pb_intin(pb, 0);
    int16_t mask = aes_pb_intin(pb, 1);
    int16_t state = aes_pb_intin(pb, 2);
    (void)clicks; // multi-click counting not modeled; the first matching transition satisfies the wait
    (void)mask;   // only the left button is tracked, same as evnt_multi's MU_BUTTON handling

    for (;;) {
        canvas_pump_messages();

        if (tos_is_halted()) {
            return;
        }

        if (mouse_button_edge(state)) {
            break;
        }
        Sleep(10);
    }

    int gx, gy;
    aes_window_cursor_pos(&gx, &gy);

    // intout[0] is the number of times the state matched; the loop breaks on the first, and multi-click counting isn't modeled.
    aes_pb_set_intout(pb, 0, 1);
    aes_pb_set_intout(pb, 1, (int16_t)gx);
    aes_pb_set_intout(pb, 2, (int16_t)gy);
    aes_pb_set_intout(pb, 3, state);
    aes_pb_set_intout(pb, 4, bios_kbd_shift_state());

    log_write(LOG_API, "evnt_button(clicks=%d, mask=%d, state=%d) -> mx=%d my=%d", clicks, mask, state, gx, gy);
}

// [FULL] Pumps host messages while waiting, so the window keeps repainting through a long timeout.
void aes_evnt_timer(const AesPB *pb) {
    uint16_t low = (uint16_t)aes_pb_intin(pb, 0);
    uint16_t high = (uint16_t)aes_pb_intin(pb, 1);
    unsigned int timer_ms = ((unsigned int)high << 16) | low;
    DWORD deadline = GetTickCount() + timer_ms;

    for (;;) {
        canvas_pump_messages();
        if (tos_is_halted()) {
            return;
        }
        if ((int)(GetTickCount() - deadline) >= 0) {
            break;
        }
        Sleep(10);
    }

    aes_pb_set_intout(pb, 0, 1); // reserved, real GEM always returns 1
    log_write(LOG_API, "evnt_timer(%ums) -> elapsed", timer_ms);
}

// [FULL] evnt_mouse(). Waits for the pointer to enter or leave one rectangle; the same condition evnt_multi() ignores when it is asked for via MU_M1/MU_M2.
void aes_evnt_mouse(const AesPB *pb) {
    int16_t flag = aes_pb_intin(pb, 0); // 0 = MO_ENTER (wait to enter rect), 1 = MO_LEAVE (wait to leave rect)
    int16_t x = aes_pb_intin(pb, 1);
    int16_t y = aes_pb_intin(pb, 2);
    int16_t w = aes_pb_intin(pb, 3);
    int16_t h = aes_pb_intin(pb, 4);

    int gx = 0, gy = 0;
    for (;;) {
        canvas_pump_messages();

        if (tos_is_halted()) {
            return;
        }

        aes_window_cursor_pos(&gx, &gy);

        int inside = (gx >= x && gx < x + w && gy >= y && gy < y + h);
        if (flag ? !inside : inside) {
            break;
        }
        Sleep(10);
    }

    int button = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) ? 1 : 0;

    aes_pb_set_intout(pb, 0, 1); // reserved, real GEM always returns 1
    aes_pb_set_intout(pb, 1, (int16_t)gx);
    aes_pb_set_intout(pb, 2, (int16_t)gy);
    aes_pb_set_intout(pb, 3, (int16_t)button);
    aes_pb_set_intout(pb, 4, bios_kbd_shift_state());

    log_write(LOG_API, "evnt_mouse(flag=%d, rect=%d,%d,%d,%d) -> mx=%d my=%d button=%d", flag, x, y, w, h, gx, gy, button);
}