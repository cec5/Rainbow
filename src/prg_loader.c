#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "m68k.h"
#include "memory.h"
#include "prg_loader.h"

#define PRG_MAGIC        0x601A
#define PRG_HEADER_SIZE  28
#define BASEPAGE_SIZE    0x100
#define DEFAULT_STACK_SIZE (64 * 1024)

typedef struct {
    uint32_t tlen;
    uint32_t dlen;
    uint32_t blen;
    uint32_t slen;
    uint16_t absflag;
} PrgHeader;

static uint32_t read_be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int parse_header(const unsigned char *buf, size_t size, PrgHeader *hdr) {
    if (size < PRG_HEADER_SIZE) {
        return 0;
    }
    if (((buf[0] << 8) | buf[1]) != PRG_MAGIC) {
        return 0;
    }

    hdr->tlen     = read_be32(buf + 2);
    hdr->dlen     = read_be32(buf + 6);
    hdr->blen     = read_be32(buf + 10);
    hdr->slen     = read_be32(buf + 14);
    hdr->absflag  = (uint16_t)((buf[26] << 8) | buf[27]);

    return 1;
}
/* The relocation table is a 32-bit offset (from the start of TEXT) to the
 * first longword needing a fixup, followed by a byte chain giving the
 * delta to the next fixup: 0 ends the chain, 1 adds 254 and keeps reading
 * (a single byte can't express deltas bigger than that). Each fixed-up
 * longword gets load_base added to whatever the compiler assumed was a
 * zero-based image. */
static void apply_relocations(const unsigned char *reloc, size_t reloc_size, unsigned int load_base) {
    if (reloc_size < 4) {
        return;
    }

    uint32_t offset = read_be32(reloc);
    if (offset == 0) {
        return;
    }

    size_t pos = 4;
    unsigned int fixup_addr = load_base + offset;

    for (;;) {
        unsigned int value = m68k_read_memory_32(fixup_addr);
        m68k_write_memory_32(fixup_addr, value + load_base);

        if (pos >= reloc_size) {
            break;
        }
        unsigned char delta = reloc[pos++];

        while (delta == 1) {
            fixup_addr += 254;
            if (pos >= reloc_size) {
                delta = 0;
                break;
            }
            delta = reloc[pos++];
        }
        if (delta == 0) {
            break;
        }
        fixup_addr += delta;
    }
}

static void write_basepage(unsigned int basepage_addr, unsigned int text_addr, unsigned int data_addr, unsigned int bss_addr, unsigned int stack_top, const PrgHeader *hdr) {
    m68k_write_memory_32(basepage_addr + 0x00, basepage_addr);        // p_lowtpa
    m68k_write_memory_32(basepage_addr + 0x04, stack_top);            // p_hitpa
    m68k_write_memory_32(basepage_addr + 0x08, text_addr);            // p_tbase
    m68k_write_memory_32(basepage_addr + 0x0C, hdr->tlen);            // p_tlen
    m68k_write_memory_32(basepage_addr + 0x10, data_addr);            // p_dbase
    m68k_write_memory_32(basepage_addr + 0x14, hdr->dlen);            // p_dlen
    m68k_write_memory_32(basepage_addr + 0x18, bss_addr);             // p_bbase
    m68k_write_memory_32(basepage_addr + 0x1C, hdr->blen);            // p_blen
    m68k_write_memory_32(basepage_addr + 0x20, basepage_addr + 0x80); // p_dta
    m68k_write_memory_32(basepage_addr + 0x24, 0);                    // p_parent
    m68k_write_memory_32(basepage_addr + 0x28, 0);                    // p_reserved
    m68k_write_memory_32(basepage_addr + 0x2C, 0);                    // p_env
    m68k_write_memory_8(basepage_addr + 0x80, 0);                     // empty command line
}

int prg_load(const char *host_path, unsigned int load_addr, PrgLoadResult *result) {
    FILE *f = fopen(host_path, "rb");
    if (!f) {
        fprintf(stderr, "[prg_loader] Could not open '%s'\n", host_path);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < PRG_HEADER_SIZE) {
        fprintf(stderr, "[prg_loader] '%s' is too small to be a GEMDOS program\n", host_path);
        fclose(f);
        return 0;
    }

    unsigned char *buf = malloc((size_t)file_size);
    if (!buf || fread(buf, 1, (size_t)file_size, f) != (size_t)file_size) {
        fprintf(stderr, "[prg_loader] Failed to read '%s'\n", host_path);
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);

    PrgHeader hdr;
    if (!parse_header(buf, (size_t)file_size, &hdr)) {
        fprintf(stderr, "[prg_loader] '%s' has no GEMDOS header (bad magic)\n", host_path);
        free(buf);
        return 0;
    }

    size_t text_off = PRG_HEADER_SIZE;
    size_t data_off = text_off + hdr.tlen;
    size_t reloc_off = data_off + hdr.dlen + hdr.slen;

    if (data_off + hdr.dlen > (size_t)file_size) {
        fprintf(stderr, "[prg_loader] '%s' is truncated (TEXT/DATA exceed file size)\n", host_path);
        free(buf);
        return 0;
    }

    unsigned int basepage_addr = load_addr;
    unsigned int text_addr = basepage_addr + BASEPAGE_SIZE;
    unsigned int data_addr = text_addr + hdr.tlen;
    unsigned int bss_addr  = data_addr + hdr.dlen;
    unsigned int stack_top = bss_addr + hdr.blen + DEFAULT_STACK_SIZE;

    if (stack_top >= MAX_MEM) {
        fprintf(stderr, "[prg_loader] '%s' does not fit in virtual RAM\n", host_path);
        free(buf);
        return 0;
    }

    for (uint32_t i = 0; i < hdr.tlen; i++) {
        m68k_write_memory_8(text_addr + i, buf[text_off + i]);
    }
    for (uint32_t i = 0; i < hdr.dlen; i++) {
        m68k_write_memory_8(data_addr + i, buf[data_off + i]);
    }
    for (uint32_t i = 0; i < hdr.blen; i++) {
        m68k_write_memory_8(bss_addr + i, 0);
    }

    if (!hdr.absflag && reloc_off < (size_t)file_size) {
        apply_relocations(buf + reloc_off, (size_t)file_size - reloc_off, text_addr);
    }

    free(buf);

    write_basepage(basepage_addr, text_addr, data_addr, bss_addr, stack_top, &hdr);

    result->basepage_addr = basepage_addr;
    result->entry_addr = text_addr;
    result->stack_addr = stack_top;

    result->text_addr = text_addr;
    result->text_len = hdr.tlen;
    result->data_addr = data_addr;
    result->data_len = hdr.dlen;
    result->bss_addr = bss_addr;
    result->bss_len = hdr.blen;

    printf("[prg_loader] Loaded '%s': TEXT=0x%08X(%u) DATA=0x%08X(%u) BSS=0x%08X(%u) entry=0x%08X\n", host_path, text_addr, hdr.tlen, data_addr, hdr.dlen, bss_addr, hdr.blen, text_addr);

    return 1;
}