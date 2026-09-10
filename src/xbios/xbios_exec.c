#include <stddef.h>
#include "xbios/xbios_exec.h"
#include "m68k.h"
#include "logger.h"

// [FULL] Rather than calling the guest routine, its address is jumped to with the trap's own return address pushed, so the routine's closing rts lands back in the caller.
unsigned int xbios_supexec(unsigned int args_addr) {
    unsigned int func = m68k_read_memory_32(args_addr);
    unsigned int sp = m68k_get_reg(NULL, M68K_REG_SP);
    unsigned int return_pc = m68k_get_reg(NULL, M68K_REG_PC);

    unsigned int new_sp = sp - 4;
    m68k_write_memory_32(new_sp, return_pc);
    m68k_set_reg(M68K_REG_SP, new_sp);
    m68k_set_reg(M68K_REG_PC, func);

    log_write(LOG_API, "Supexec(0x%08X) -> entering supervisor function, will resume at 0x%08X via its own rts", func, return_pc);

    return m68k_get_reg(NULL, M68K_REG_D0);
}