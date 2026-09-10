#ifndef BIOS_KBD_H
#define BIOS_KBD_H

// Kbshift(mode): -1 polls the live Shift/Ctrl/Alt/Caps state; any other value overrides the stored state instead.
unsigned int bios_kbshift(unsigned int args_addr);

// The AES event calls fill their kstate field from this too, so the two cannot disagree about modifier state.
unsigned char bios_kbd_shift_state(void);

#endif