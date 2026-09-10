#ifndef BIOS_CONSOLE_H
#define BIOS_CONSOLE_H

// Bconstat(WORD dev): -1 if a character is waiting to be read, 0 otherwise.
unsigned int bios_bconstat(unsigned int args_addr);

// Bconin(WORD dev): blocks until a character is available, returns it in the low byte of D0.
unsigned int bios_bconin(unsigned int args_addr);

// Bconout(WORD dev, WORD chr): writes a single character to dev.
unsigned int bios_bconout(unsigned int args_addr);

// Bcostat(WORD dev): -1 if dev is ready to accept output, 0 otherwise.
unsigned int bios_bcostat(unsigned int args_addr);

#endif