#ifndef PRG_LOADER_H
#define PRG_LOADER_H

typedef struct {
    unsigned int basepage_addr;
    unsigned int entry_addr;  // Where PC should start: basepage_addr + 0x100
    unsigned int stack_addr;  // Initial SSP: top of the allocated TPA

    unsigned int text_addr;
    unsigned int text_len;
    unsigned int data_addr;
    unsigned int data_len;
    unsigned int bss_addr;
    unsigned int bss_len;
} PrgLoadResult;

// Applies relocations and builds a basepage as it loads. Returns 1 on success, 0 on failure.
int prg_load(const char *host_path, unsigned int load_addr, PrgLoadResult *result);

#endif