#include <stdint.h>
#include "m68k.h"
#include "bios/bios_console.h"
#include "gemdos/gemdos_file.h"
#include "logger.h"

// Only the console/keyboard (dev 2) is modeled; PRT/AUX/MIDI/IKBD are ignored. Console traffic goes through gemdos_file_read/write, the same handle 0/1 path Cconws and Fwrite use.
#define BIOS_DEV_CON 2

// [PARTIAL] Always claims a character is waiting, since a non-consuming peek at stdin would be needed to answer honestly. A guest polling this to avoid blocking will still block in Bconin.
unsigned int bios_bconstat(unsigned int args_addr) {
    int16_t dev = (int16_t)m68k_read_memory_16(args_addr);
    if (dev != BIOS_DEV_CON) {
        return 0;
    }

    return 0xFFFFFFFF;
}

// [FULL]
unsigned int bios_bconin(unsigned int args_addr) {
    int16_t dev = (int16_t)m68k_read_memory_16(args_addr);
    if (dev != BIOS_DEV_CON) {
        return 0;
    }

    unsigned char c = 0;
    gemdos_file_read(0, &c, 1);
    return c;
}

// [FULL]
unsigned int bios_bconout(unsigned int args_addr) {
    int16_t dev = (int16_t)m68k_read_memory_16(args_addr);
    int16_t chr = (int16_t)m68k_read_memory_16(args_addr + 2);
    if (dev != BIOS_DEV_CON) {
        log_write(LOG_INFO, "Bconout: device %d not modeled, ignored", dev);
        return 0;
    }

    unsigned char c = (unsigned char)chr;
    gemdos_file_write(1, &c, 1);
    return 0;
}

// [FULL] Host console output is always writable, so "ready" is the honest answer here.
unsigned int bios_bcostat(unsigned int args_addr) {
    int16_t dev = (int16_t)m68k_read_memory_16(args_addr);
    if (dev != BIOS_DEV_CON) {
        return 0;
    }
    return 0xFFFFFFFF;
}