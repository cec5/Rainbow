#include <stdio.h>
#include <stdlib.h>
#include "m68k.h"
#include "gemdos/gemdos_console.h"
#include "gemdos/gemdos_file.h"
#include "logger.h"

#define STRING_MAX_LEN (1u * 1024u * 1024u)

// [FULL] Cconout(WORD ch)
unsigned int gemdos_cconout(unsigned int args_addr) {
    unsigned char ch = (unsigned char)m68k_read_memory_16(args_addr);

    fflush(stdout);
    gemdos_file_write(1, &ch, 1);

    log_write(LOG_API, "Cconout(0x%02X) -> Fwrite(handle=1)", ch);
    return 0;
}

// [FULL] Cconws(BYTE *str). The length cap guards against an unterminated string walking the whole address space.
unsigned int gemdos_cconws(unsigned int args_addr) {
    unsigned int str_addr = m68k_read_memory_32(args_addr);

    unsigned int len = 0;
    while (len < STRING_MAX_LEN && m68k_read_memory_8(str_addr + len) != 0) {
        len++;
    }

    unsigned char *buffer = malloc(len);
    if (!buffer) {
        log_write(LOG_ERROR, "Cconws -> host malloc(%u) failed", len);
        return 0;
    }
    for (unsigned int i = 0; i < len; i++) {
        buffer[i] = (unsigned char)m68k_read_memory_8(str_addr + i);
    }

    fflush(stdout); // Keeps the layer's own output in order with the guest's
    log_write(LOG_API, "Cconws(%u bytes) -> Fwrite(handle=1)", len);
    gemdos_file_write(1, buffer, len);

    free(buffer);
    return 0;
}