#include <stdint.h>
#include <windows.h>
#include "bios/bios_kbd.h"
#include "m68k.h"
#include "logger.h"

#define K_RSHIFT   0x01
#define K_LSHIFT   0x02
#define K_CTRL     0x04
#define K_ALT      0x08
#define K_CAPSLOCK 0x10

static unsigned char s_kbshift_state = 0;

// Caps lock is a toggle, so it is read from the latched key state rather than the live one.
static unsigned char poll_live_state(void) {
    unsigned char state = 0;
    if (GetAsyncKeyState(VK_RSHIFT)  & 0x8000) state |= K_RSHIFT;
    if (GetAsyncKeyState(VK_LSHIFT)  & 0x8000) state |= K_LSHIFT;
    if (GetAsyncKeyState(VK_CONTROL) & 0x8000) state |= K_CTRL;
    if (GetAsyncKeyState(VK_MENU)    & 0x8000) state |= K_ALT;
    if (GetKeyState(VK_CAPITAL) & 0x0001) state |= K_CAPSLOCK;
    return state;
}

// Shared with the AES event calls, so Kbshift() and graf_mkstate() cannot disagree about modifier state.
unsigned char bios_kbd_shift_state(void) {
    return poll_live_state();
}

// [FULL] Mode -1 queries and returns the live state; any other value sets the state and returns the previous one.
unsigned int bios_kbshift(unsigned int args_addr) {
    int16_t mode = (int16_t)m68k_read_memory_16(args_addr);
    unsigned char old_state = s_kbshift_state;

    if (mode == -1) {
        s_kbshift_state = poll_live_state();
        log_write(LOG_API, "Kbshift(mode=-1) -> polled 0x%02X", s_kbshift_state);
        return s_kbshift_state;
    }

    s_kbshift_state = (unsigned char)mode;
    log_write(LOG_API, "Kbshift(mode=%d) -> was 0x%02X, now 0x%02X", mode, old_state, s_kbshift_state);
    return old_state;
}