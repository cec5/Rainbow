#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory.h"
#include "m68k.h"
#include "logger.h"
#include "tos_layer.h"

uint8_t virtual_ram[MAX_MEM];

static unsigned int s_code_start = 0;
static unsigned int s_code_end = 0;

static unsigned char *s_executed = NULL;

void memory_init(void) {
    memset(virtual_ram, 0, MAX_MEM);
}

void memory_watch_code(unsigned int start, unsigned int end) {
    free(s_executed);
    s_executed = NULL;
    s_code_start = start;
    s_code_end = end;
    if (end > start) {
        s_executed = calloc(((size_t)(end - start) + 7) / 8, 1);
    }
}

void memory_mark_executed(unsigned int pc) {
    if (!s_executed || pc < s_code_start || pc >= s_code_end) {
        return;
    }
    unsigned int bit = pc - s_code_start;
    s_executed[bit / 8] |= (unsigned char)(1u << (bit % 8));
}

static int any_byte_executed(unsigned int address, unsigned int size) {
    if (!s_executed) {
        return 0;
    }
    unsigned int lo = address > s_code_start ? address : s_code_start;
    unsigned int hi = (address + size) < s_code_end ? (address + size) : s_code_end;
    for (unsigned int a = lo; a < hi; a++) {
        unsigned int bit = a - s_code_start;
        if (s_executed[bit / 8] & (unsigned char)(1u << (bit % 8))) {
            return 1;
        }
    }
    return 0;
}

static int s_code_write_reported = 0;

static void check_code_write(unsigned int address, unsigned int size, unsigned int value) {
    if (s_code_end == 0 || address + size <= s_code_start || address >= s_code_end) {
        return;
    }
    if (!any_byte_executed(address, size)) {
        return;
    }
    log_write(LOG_ERROR, "CODE CORRUPTION: %u-byte write of 0x%X to 0x%08X, inside TEXT (0x%08X-0x%08X), from PC=0x%08X", size, value, address, s_code_start, s_code_end, m68k_get_reg(NULL, M68K_REG_PPC));

    if (!s_code_write_reported) {
        s_code_write_reported = 1;
        log_write(LOG_ERROR, "  registers at first corrupting write: D0=0x%08X D1=0x%08X A0=0x%08X A1=0x%08X A2=0x%08X A3=0x%08X A6=0x%08X A7=0x%08X", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3), m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_SP));
        tos_dump_pc_history();
    }
}

static inline int check_bounds(unsigned int address) {
    if (address >= MAX_MEM) {
        printf("Out of bounds memory access at 0x%08X\n", address);
        return 0;
    }
    return 1;
}

unsigned int m68k_read_memory_8(unsigned int address) {
    if (!check_bounds(address)) return 0;
    return virtual_ram[address];
}

unsigned int m68k_read_memory_16(unsigned int address) {
    if (!check_bounds(address + 1)) return 0;
    return (virtual_ram[address] << 8) | virtual_ram[address + 1];
}

unsigned int m68k_read_memory_32(unsigned int address) {
    if (!check_bounds(address + 3)) return 0;
    return (virtual_ram[address] << 24) | 
           (virtual_ram[address + 1] << 16) | 
           (virtual_ram[address + 2] << 8) | 
           virtual_ram[address + 3];
}

void m68k_write_memory_8(unsigned int address, unsigned int value) {
    if (!check_bounds(address)) return;
    check_code_write(address, 1, value);
    virtual_ram[address] = value & 0xFF;
}

void m68k_write_memory_16(unsigned int address, unsigned int value) {
    if (!check_bounds(address + 1)) return;
    check_code_write(address, 2, value);
    virtual_ram[address]     = (value >> 8) & 0xFF;
    virtual_ram[address + 1] = value & 0xFF;
}

void m68k_write_memory_32(unsigned int address, unsigned int value) {
    if (!check_bounds(address + 3)) return;
    check_code_write(address, 4, value);
    virtual_ram[address]     = (value >> 24) & 0xFF;
    virtual_ram[address + 1] = (value >> 16) & 0xFF;
    virtual_ram[address + 2] = (value >> 8) & 0xFF;
    virtual_ram[address + 3] = value & 0xFF;
}

// Disassembler reads just alias the normal read path for now, which is fine since nothing here has read side effects to worry about.
unsigned int m68k_read_disassembler_8(unsigned int address) {
    return m68k_read_memory_8(address);
}

unsigned int m68k_read_disassembler_16(unsigned int address) {
    return m68k_read_memory_16(address);
}

unsigned int m68k_read_disassembler_32(unsigned int address) {
    return m68k_read_memory_32(address);
}