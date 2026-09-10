#include <stddef.h>
#include "gemdos/gemdos_proc.h"
#include "m68k.h"
#include "logger.h"
#include "tos_layer.h"

#define SUP_SET     0u
#define SUP_INQUIRE 1u

// [FULL] Serves both Pterm0 and Pterm. The exit code Pterm carries is dropped, as there is no parent process to receive it.
unsigned int gemdos_pterm(unsigned int args_addr) {
    (void)args_addr;
    tos_request_halt();
    log_write(LOG_API, "Pterm -> halt requested");
    return 0;
}

// [PARTIAL] Only ever enters or stays in supervisor mode. Real Super() toggles back to user mode when handed the SSP it previously returned; here that call re-enters supervisor instead. See Chapter 7 of the report for why.
unsigned int gemdos_super(unsigned int args_addr) {
    unsigned int stack = m68k_read_memory_32(args_addr);
    unsigned int sr = m68k_get_reg(NULL, M68K_REG_SR);
    int was_super = (sr & SR_S_BIT) != 0;
    unsigned int old_sp = m68k_get_reg(NULL, M68K_REG_SP); // the caller's own active A7 at call time

    if (stack == SUP_INQUIRE) {
        log_write(LOG_API, "Super(SUP_INQUIRE) -> %d", was_super);
        return was_super ? 1u : 0u;
    }

    tos_set_sr(sr | SR_S_BIT);
    if (stack != SUP_SET) {
        m68k_set_reg(M68K_REG_SP, stack); // caller-supplied SSP, overriding GEMDOS's default
    }
    log_write(LOG_API, "Super(0x%08X) -> now supervisor, old sp=0x%08X", stack, old_sp);
    return old_sp;
}