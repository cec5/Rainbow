#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>

#define MAX_MEM (16 * 1024 * 1024)

extern uint8_t virtual_ram[MAX_MEM];

void memory_init(void);
void memory_watch_code(unsigned int start, unsigned int end);

void memory_mark_executed(unsigned int pc);

#endif