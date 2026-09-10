#ifndef GEMDOS_MEM_H
#define GEMDOS_MEM_H

#include <stdint.h>

/* Lays the pool out as one free block. The caller must then immediately Malloc()
 * the program's own TPA, mirroring the live allocation a real Pexec() hands a new
 * process, so Mfree and Mshrink can later recognize the basepage as theirs. */
void gemdos_mem_init(unsigned int heap_start, unsigned int heap_end);

// A size of -1 returns the largest free block instead of allocating.
unsigned int gemdos_mem_malloc(int32_t size);

// Returns 0, or a negative GEMDOS error if addr is not a live allocation.
int32_t gemdos_mem_free(unsigned int addr);

// Growing is not supported and is reported as success without extending the block.
int32_t gemdos_mem_shrink(unsigned int addr, uint32_t newsize);

// GEMDOS trap handlers (TRAP #1)
unsigned int gemdos_malloc(unsigned int args_addr);
unsigned int gemdos_mfree(unsigned int args_addr);
unsigned int gemdos_mshrink(unsigned int args_addr);

#endif