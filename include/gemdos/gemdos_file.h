#ifndef GEMDOS_FILE_H
#define GEMDOS_FILE_H

#include <stdint.h>

// Binds handles 0/1/2 to the host's standard streams. Call once at startup.
void gemdos_file_init(void);

// mode 0=read-only, 1=write-only, 2=read/write. Returns a handle or a negative GEMDOS error.
int32_t gemdos_file_open(const char *path, int16_t mode);

// Attribute bits are accepted but not applied to the host file.
int32_t gemdos_file_create(const char *path, int16_t attrib);

// Handles 0/1/2 stay bound rather than closing, so a guest that closes stdout keeps its console.
int32_t gemdos_file_close(int16_t handle);

// buf must already be sized for count bytes.
int32_t gemdos_file_read(int16_t handle, unsigned char *buf, uint32_t count);

// Handles 1/2 go through the console API, so output stays consistent whichever GEMDOS call produced it.
int32_t gemdos_file_write(int16_t handle, const unsigned char *buf, uint32_t count);

// mode 0=SEEK_SET, 1=SEEK_CUR, 2=SEEK_END.
int32_t gemdos_file_seek(int16_t handle, int32_t offset, int16_t mode);

// GEMDOS trap handlers (TRAP #1)
unsigned int gemdos_fcreate(unsigned int args_addr);
unsigned int gemdos_fopen(unsigned int args_addr);
unsigned int gemdos_fclose(unsigned int args_addr);
unsigned int gemdos_fread(unsigned int args_addr);
unsigned int gemdos_fwrite(unsigned int args_addr);
unsigned int gemdos_fseek(unsigned int args_addr);
unsigned int gemdos_fcntl(unsigned int args_addr);

#endif