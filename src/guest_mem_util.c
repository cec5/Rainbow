#include "m68k.h"
#include "guest_mem_util.h"

unsigned int guest_read_cstring(unsigned int addr, char *out, size_t max_len) {
    unsigned int i = 0;
    while (i < max_len - 1) {
        unsigned char c = (unsigned char)m68k_read_memory_8(addr + i);
        if (c == 0) {
            break;
        }
        out[i] = (char)c;
        i++;
    }
    out[i] = '\0';
    return i;
}

void guest_write_cstring(unsigned int addr, const char *src, size_t max_len) {
    if (max_len == 0) {
        return;
    }

    size_t i = 0;
    while (i < max_len - 1 && src[i] != '\0') {
        m68k_write_memory_8(addr + i, (unsigned char)src[i]);
        i++;
    }
    m68k_write_memory_8(addr + i, 0);
}