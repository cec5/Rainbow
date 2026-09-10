#ifndef GUEST_MEM_UTIL_H
#define GUEST_MEM_UTIL_H

#include <stddef.h>

unsigned int guest_read_cstring(unsigned int addr, char *out, size_t max_len);

void guest_write_cstring(unsigned int addr, const char *src, size_t max_len);

#endif