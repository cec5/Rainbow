#include "xbios/xbios.h"
#include "xbios/xbios_exec.h"
#include "xbios/xbios_screen.h"
#include "trap_table.h"

static const TrapCall s_calls[] = {
    { 0x04, "Getrez",  xbios_getrez  },
    { 0x26, "Supexec", xbios_supexec },
};

static const TrapTable s_table = {
    "XBIOS", "xbios", s_calls, sizeof(s_calls) / sizeof(s_calls[0])
};

unsigned int xbios_dispatch(unsigned int func_num, unsigned int args_addr) {
    return trap_table_dispatch(&s_table, func_num, args_addr);
}