#include <stdio.h>
#include "trap_table.h"
#include "logger.h"

unsigned int trap_table_dispatch(const TrapTable *table, unsigned int func_num, unsigned int args_addr) {
    const TrapCall *call = NULL;
    for (size_t i = 0; i < table->count; i++) {
        if (table->calls[i].func_num == func_num) {
            call = &table->calls[i];
            break;
        }
    }

    const char *name = call ? call->name : "Unknown";
    log_write(LOG_API, "%s %s (0x%02X) called", table->label, name, func_num);

    unsigned int result = 0;
    if (call && call->handler) {
        result = call->handler(args_addr);
    } else {
        printf("[%s] Unimplemented %s function 0x%02X\n", table->tag, table->label, func_num);
        log_write(LOG_ERROR, "%s function 0x%02X is unimplemented", table->label, func_num);
    }

    log_write(LOG_API, "%s %s (0x%02X) returned D0=0x%08X", table->label, name, func_num, result);

    return result;
}