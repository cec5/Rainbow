#ifndef TRAP_TABLE_H
#define TRAP_TABLE_H

#include <stddef.h>

/* Each handler carries a status tag at its definition: [FULL] behaves as TOS
 * specifies within the layer's scope, [PARTIAL] works but with a stated gap,
 * [NO-OP] is accepted and answered but changes nothing. A NULL handler here is
 * the fourth case: unimplemented, named only so the log can report it. */
typedef struct {
    unsigned int func_num;
    const char  *name;
    unsigned int (*handler)(unsigned int args_addr);
} TrapCall;

typedef struct {
    const char     *label; // "GEMDOS", as the log names the subsystem
    const char     *tag;   // "gemdos", the lowercase console prefix used across the layer
    const TrapCall *calls;
    size_t          count;
} TrapTable;

// Looks the function up, logs the call and its result, and returns what goes back in D0.
unsigned int trap_table_dispatch(const TrapTable *table, unsigned int func_num, unsigned int args_addr);

#endif