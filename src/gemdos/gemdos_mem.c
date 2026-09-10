#include <stdlib.h>
#include "m68k.h"
#include "gemdos/gemdos_mem.h"
#include "gemdos/gemdos_errno.h"
#include "logger.h"

#define ALIGN_UP(n, a) (((n) + (a) - 1) & ~((unsigned int)(a) - 1))

// ALLOCATOR
// First-fit free-list allocator over the pool

typedef struct MemBlock {
    unsigned int addr;
    unsigned int size;
    int is_free;
    struct MemBlock *next;
} MemBlock;

static MemBlock *s_head = NULL;

void gemdos_mem_init(unsigned int heap_start, unsigned int heap_end) {
    while (s_head) {
        MemBlock *next = s_head->next;
        free(s_head);
        s_head = next;
    }

    if (heap_end <= heap_start) {
        return;
    }

    s_head = malloc(sizeof(MemBlock));
    s_head->addr = heap_start;
    s_head->size = heap_end - heap_start;
    s_head->is_free = 1;
    s_head->next = NULL;
}

static unsigned int largest_free_block(void) {
    unsigned int largest = 0;
    for (MemBlock *b = s_head; b; b = b->next) {
        if (b->is_free && b->size > largest) {
            largest = b->size;
        }
    }
    return largest;
}

// Splits the tail of b into its own free block, so an allocation smaller than the block it landed in does not waste the remainder.
static void split_tail(MemBlock *b, unsigned int used) {
    if (used >= b->size) {
        return;
    }

    MemBlock *remainder = malloc(sizeof(MemBlock));
    remainder->addr = b->addr + used;
    remainder->size = b->size - used;
    remainder->is_free = 1;
    remainder->next = b->next;

    b->size = used;
    b->next = remainder;
}

// Merges b with the free blocks that follow it, so repeated free/shrink cycles cannot fragment the pool into unusable slivers.
static void coalesce_forward(MemBlock *b) {
    while (b && b->is_free && b->next && b->next->is_free) {
        MemBlock *victim = b->next;
        b->size += victim->size;
        b->next = victim->next;
        free(victim);
    }
}

unsigned int gemdos_mem_malloc(int32_t size) {
    if (size == -1) {
        unsigned int largest = largest_free_block();
        log_write(LOG_API, "Malloc(-1) -> largest free block=%u bytes", largest);
        return largest;
    }
    if (size <= 0) {
        log_write(LOG_ERROR, "Malloc(%d) -> invalid size", size);
        return 0;
    }

    unsigned int want = ALIGN_UP((unsigned int)size, 4);

    for (MemBlock *b = s_head; b; b = b->next) {
        if (!b->is_free || b->size < want) {
            continue;
        }

        split_tail(b, want);
        b->is_free = 0;
        log_write(LOG_API, "Malloc(%d) -> addr=0x%08X (%u bytes reserved)", size, b->addr, want);
        return b->addr;
    }

    log_write(LOG_ERROR, "Malloc(%d) -> no free block large enough", size);
    return 0;
}

static MemBlock *find_block(unsigned int addr, MemBlock **out_prev) {
    MemBlock *prev = NULL;
    for (MemBlock *b = s_head; b; b = b->next) {
        if (b->addr == addr) {
            if (out_prev) {
                *out_prev = prev;
            }
            return b;
        }
        prev = b;
    }
    return NULL;
}

int32_t gemdos_mem_free(unsigned int addr) {
    MemBlock *prev = NULL;
    MemBlock *b = find_block(addr, &prev);
    if (!b || b->is_free) {
        log_write(LOG_ERROR, "Mfree(0x%08X) -> not a live allocation", addr);
        return EIMBA;
    }

    b->is_free = 1;
    coalesce_forward(b);
    if (prev) {
        coalesce_forward(prev);
    }

    log_write(LOG_API, "Mfree(0x%08X) -> freed", addr);
    return 0;
}

int32_t gemdos_mem_shrink(unsigned int addr, uint32_t newsize) {
    MemBlock *b = find_block(addr, NULL);
    if (!b || b->is_free) {
        log_write(LOG_ERROR, "Mshrink(0x%08X, %u) -> not a live allocation", addr, newsize);
        return EIMBA;
    }

    unsigned int want = ALIGN_UP(newsize, 4);
    if (want >= b->size) {
        log_write(LOG_API, "Mshrink(0x%08X, %u) -> no-op (nothing to shrink)", addr, newsize);
        return 0;
    }

    split_tail(b, want);
    coalesce_forward(b->next);

    log_write(LOG_API, "Mshrink(0x%08X, %u) -> shrunk to %u bytes", addr, newsize, want);
    return 0;
}

// GEMDOS TRAP HANDLERS

// [FULL] Malloc(LONG number). A size of -1 queries the largest free block rather than allocating.
unsigned int gemdos_malloc(unsigned int args_addr) {
    int32_t size = (int32_t)m68k_read_memory_32(args_addr);
    return gemdos_mem_malloc(size);
}

// [FULL] Mfree(VOID *block)
unsigned int gemdos_mfree(unsigned int args_addr) {
    unsigned int block = m68k_read_memory_32(args_addr);
    return (unsigned int)gemdos_mem_free(block);
}

// [FULL] Mshrink(WORD dummy, VOID *block, LONG newsize). A request to grow is reported as success without extending the block, since only shrinking is defined.
unsigned int gemdos_mshrink(unsigned int args_addr) {
    unsigned int block = m68k_read_memory_32(args_addr + 2);
    uint32_t newsize = m68k_read_memory_32(args_addr + 6);
    return (unsigned int)gemdos_mem_shrink(block, newsize);
}