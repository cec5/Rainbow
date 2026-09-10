#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include "m68k.h"
#include "gemdos/gemdos_file.h"
#include "gemdos/gemdos_errno.h"
#include "guest_mem_util.h"
#include "host_paths.h"
#include "logger.h"

#define MAX_HANDLES 64
#define MAX_PATH_LEN 1024

typedef struct {
    HANDLE win_handle;
    int in_use;
} FileHandle;

static FileHandle s_handles[MAX_HANDLES];

// HOST FILE HELPERS

void gemdos_file_init(void) {
    for (int i = 0; i < MAX_HANDLES; i++) {
        s_handles[i].win_handle = INVALID_HANDLE_VALUE;
        s_handles[i].in_use = 0;
    }

    s_handles[0].win_handle = GetStdHandle(STD_INPUT_HANDLE);
    s_handles[0].in_use = 1;
    s_handles[1].win_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    s_handles[1].in_use = 1;
    s_handles[2].win_handle = GetStdHandle(STD_ERROR_HANDLE);
    s_handles[2].in_use = 1;
}

static int32_t alloc_handle(HANDLE win_handle) {
    for (int i = 3; i < MAX_HANDLES; i++) {
        if (!s_handles[i].in_use) {
            s_handles[i].win_handle = win_handle;
            s_handles[i].in_use = 1;
            return i;
        }
    }
    return ENHNDL;
}

static int handle_valid(int16_t handle) {
    return handle >= 0 && handle < MAX_HANDLES && s_handles[handle].in_use;
}

int32_t gemdos_file_open(const char *path, int16_t mode) {
    // Only bits 0-2 of mode are the access code; the sharing (4-6) and inheritance (7) fields aren't modeled.
    int access_code = mode & 0x07;
    DWORD access = GENERIC_READ;
    if (access_code == 1) {
        access = GENERIC_WRITE;
    } else if (access_code == 2) {
        access = GENERIC_READ | GENERIC_WRITE;
    }

    HANDLE h = CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        fprintf(stderr, "[gemdos_file] Fopen('%s', mode=%d) failed, GetLastError=%lu\n", path, mode, (unsigned long)err);
        log_write(LOG_ERROR, "Fopen('%s', mode=%d) -> CreateFileA(OPEN_EXISTING) failed, GetLastError=%lu", path, mode, (unsigned long)err);
        return EFILNF;
    }

    int32_t handle = alloc_handle(h);
    if (handle < 0) {
        CloseHandle(h);
        log_write(LOG_ERROR, "Fopen('%s') opened but the handle table is full", path);
    } else {
        log_write(LOG_API, "Fopen('%s', mode=%d) -> CreateFileA(OPEN_EXISTING) -> handle=%d", path, mode, handle);
    }
    return handle;
}

int32_t gemdos_file_create(const char *path, int16_t attrib) {
    (void)attrib; // DOS-style attribute bits aren't modeled on the host

    HANDLE h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        log_write(LOG_ERROR, "Fcreate('%s') -> CreateFileA(CREATE_ALWAYS) failed, GetLastError=%lu", path, (unsigned long)GetLastError());
        return EFILNF;
    }

    int32_t handle = alloc_handle(h);
    if (handle < 0) {
        CloseHandle(h);
        log_write(LOG_ERROR, "Fcreate('%s') created but the handle table is full", path);
    } else {
        log_write(LOG_API, "Fcreate('%s') -> CreateFileA(CREATE_ALWAYS) -> handle=%d", path, handle);
    }
    return handle;
}

int32_t gemdos_file_close(int16_t handle) {
    if (!handle_valid(handle)) {
        log_write(LOG_ERROR, "Fclose(handle=%d) -> invalid handle", handle);
        return EIHNDL;
    }

    // Closing a standard handle would leave the guest unable to write for the rest of the run, so they stay bound.
    if (handle <= 2) {
        log_write(LOG_API, "Fclose(handle=%d) -> standard handle, left bound", handle);
        return 0;
    }

    CloseHandle(s_handles[handle].win_handle);
    s_handles[handle].in_use = 0;
    s_handles[handle].win_handle = INVALID_HANDLE_VALUE;
    log_write(LOG_API, "Fclose(handle=%d) -> CloseHandle", handle);
    return 0;
}

int32_t gemdos_file_read(int16_t handle, unsigned char *buf, uint32_t count) {
    if (!handle_valid(handle)) {
        log_write(LOG_ERROR, "Fread(handle=%d) -> invalid handle", handle);
        return EIHNDL;
    }

    DWORD read_count = 0;
    if (!ReadFile(s_handles[handle].win_handle, buf, count, &read_count, NULL)) {
        log_write(LOG_ERROR, "Fread(handle=%d, count=%u) -> ReadFile failed, GetLastError=%lu", handle, count, (unsigned long)GetLastError());
        return EIHNDL;
    }
    log_write(LOG_API, "Fread(handle=%d, count=%u) -> ReadFile -> read=%lu", handle, count, (unsigned long)read_count);
    return (int32_t)read_count;
}

int32_t gemdos_file_write(int16_t handle, const unsigned char *buf, uint32_t count) {
    if (!handle_valid(handle)) {
        log_write(LOG_ERROR, "Fwrite(handle=%d) -> invalid handle", handle);
        return EIHNDL;
    }

    DWORD written = 0;
    if (handle == 1 || handle == 2) {
        if (WriteConsoleA(s_handles[handle].win_handle, buf, count, &written, NULL)) {
            log_write(LOG_API, "Fwrite(handle=%d, count=%u) -> WriteConsoleA -> wrote=%lu", handle, count, (unsigned long)written);
            return (int32_t)written;
        }
    }

    if (!WriteFile(s_handles[handle].win_handle, buf, count, &written, NULL)) {
        log_write(LOG_ERROR, "Fwrite(handle=%d, count=%u) -> WriteFile failed, GetLastError=%lu", handle, count, (unsigned long)GetLastError());
        return EIHNDL;
    }
    log_write(LOG_API, "Fwrite(handle=%d, count=%u) -> WriteFile -> wrote=%lu", handle, count, (unsigned long)written);
    return (int32_t)written;
}

int32_t gemdos_file_seek(int16_t handle, int32_t offset, int16_t mode) {
    if (!handle_valid(handle)) {
        log_write(LOG_ERROR, "Fseek(handle=%d) -> invalid handle", handle);
        return EIHNDL;
    }

    DWORD method = FILE_BEGIN;
    if (mode == 1) {
        method = FILE_CURRENT;
    } else if (mode == 2) {
        method = FILE_END;
    }

    DWORD new_pos = SetFilePointer(s_handles[handle].win_handle, offset, NULL, method);
    if (new_pos == INVALID_SET_FILE_POINTER) {
        log_write(LOG_ERROR, "Fseek(handle=%d, offset=%d, mode=%d) -> SetFilePointer failed, GetLastError=%lu", handle, offset, mode, (unsigned long)GetLastError());
        return EIHNDL;
    }
    log_write(LOG_API, "Fseek(handle=%d, offset=%d, mode=%d) -> SetFilePointer -> pos=%lu", handle, offset, mode, (unsigned long)new_pos);
    return (int32_t)new_pos;
}

// GEMDOS TRAP HANDLERS

// [PARTIAL] Fcreate(BYTE *fname, WORD attr). The DOS attribute bits are ignored; every file is created normal.
unsigned int gemdos_fcreate(unsigned int args_addr) {
    unsigned int fname_addr = m68k_read_memory_32(args_addr);
    int16_t attrib = (int16_t)m68k_read_memory_16(args_addr + 4);

    char guest_path[MAX_PATH_LEN];
    guest_read_cstring(fname_addr, guest_path, sizeof(guest_path));
    char path[MAX_PATH_LEN];
    gemdos_translate_path(guest_path, path, sizeof(path));

    return (unsigned int)gemdos_file_create(path, attrib);
}

// [PARTIAL] Fopen(BYTE *fname, WORD mode). The sharing and inheritance fields are read past but not enforced; every handle is opened fully shared.
unsigned int gemdos_fopen(unsigned int args_addr) {
    unsigned int fname_addr = m68k_read_memory_32(args_addr);
    int16_t mode = (int16_t)m68k_read_memory_16(args_addr + 4);

    char guest_path[MAX_PATH_LEN];
    guest_read_cstring(fname_addr, guest_path, sizeof(guest_path));
    char path[MAX_PATH_LEN];
    gemdos_translate_path(guest_path, path, sizeof(path));

    return (unsigned int)gemdos_file_open(path, mode);
}

// [FULL] Fclose(WORD handle)
unsigned int gemdos_fclose(unsigned int args_addr) {
    int16_t handle = (int16_t)m68k_read_memory_16(args_addr);
    return (unsigned int)gemdos_file_close(handle);
}

// [FULL] Fread(WORD handle, LONG count, VOID *buf). Staged through a host buffer because the guest's destination is emulated memory, not directly writable by ReadFile.
unsigned int gemdos_fread(unsigned int args_addr) {
    int16_t handle = (int16_t)m68k_read_memory_16(args_addr);
    uint32_t count = m68k_read_memory_32(args_addr + 2);
    unsigned int buf_addr = m68k_read_memory_32(args_addr + 6);

    unsigned char *buf = malloc(count);
    if (!buf) {
        return 0;
    }

    int32_t got = gemdos_file_read(handle, buf, count);
    if (got > 0) {
        for (int32_t i = 0; i < got; i++) {
            m68k_write_memory_8(buf_addr + i, buf[i]);
        }
    }

    free(buf);
    return (unsigned int)got;
}

// [FULL] Fwrite(WORD handle, LONG count, VOID *buf). Console writes flush stdout first so the layer's own printf output stays in order with the guest's.
unsigned int gemdos_fwrite(unsigned int args_addr) {
    int16_t handle = (int16_t)m68k_read_memory_16(args_addr);
    uint32_t count = m68k_read_memory_32(args_addr + 2);
    unsigned int buf_addr = m68k_read_memory_32(args_addr + 6);

    unsigned char *buf = malloc(count);
    if (!buf) {
        return 0;
    }
    for (uint32_t i = 0; i < count; i++) {
        buf[i] = (unsigned char)m68k_read_memory_8(buf_addr + i);
    }

    if (handle == 1 || handle == 2) {
        fflush(stdout);
    }
    int32_t written = gemdos_file_write(handle, buf, count);

    free(buf);
    return (unsigned int)written;
}

// [FULL] Fseek(LONG offset, WORD handle, WORD seekmode)
unsigned int gemdos_fseek(unsigned int args_addr) {
    int32_t offset = (int32_t)m68k_read_memory_32(args_addr);
    int16_t handle = (int16_t)m68k_read_memory_16(args_addr + 4);
    int16_t mode = (int16_t)m68k_read_memory_16(args_addr + 6);

    return (unsigned int)gemdos_file_seek(handle, offset, mode);
}

// [NO-OP] A MiNT-era call the C runtime makes on the standard handles during startup; accepted so the guest does not read a failure and give up.
unsigned int gemdos_fcntl(unsigned int args_addr) {
    int16_t handle = (int16_t)m68k_read_memory_16(args_addr);
    int16_t cmd = (int16_t)m68k_read_memory_16(args_addr + 6);

    log_write(LOG_API, "Fcntl(handle=%d, cmd=0x%04X) -> accepted, not otherwise modeled", handle, (uint16_t)cmd);
    return 0;
}