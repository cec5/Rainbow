#include "bios/bios.h"
#include "bios/bios_console.h"
#include "bios/bios_disk.h"
#include "bios/bios_kbd.h"
#include "trap_table.h"

static const TrapCall s_calls[] = {
    { 0x01, "Bconstat", bios_bconstat },
    { 0x02, "Bconin",   bios_bconin   },
    { 0x03, "Bconout",  bios_bconout  },
    { 0x08, "Bcostat",  bios_bcostat  },
    { 0x0A, "Drvmap",   bios_drvmap   },
    { 0x0B, "Kbshift",  bios_kbshift  },
};

static const TrapTable s_table = {
    "BIOS", "bios", s_calls, sizeof(s_calls) / sizeof(s_calls[0])
};

unsigned int bios_dispatch(unsigned int func_num, unsigned int args_addr) {
    return trap_table_dispatch(&s_table, func_num, args_addr);
}