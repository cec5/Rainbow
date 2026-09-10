#include <stdint.h>
#include <windows.h>
#include "gemdos/gemdos_dir.h"
#include "gemdos/gemdos_errno.h"
#include "m68k.h"
#include "guest_mem_util.h"
#include "host_paths.h"
#include "logger.h"

// The layer models neither multiple drives nor a current directory: a guest's files live wherever its own executable does, so there is nothing to report beyond one fixed drive and a root path.
#define FIXED_DRIVE_CODE 2u

// [FULL] Dgetdrv()
unsigned int gemdos_dgetdrv(unsigned int args_addr) {
    (void)args_addr; // Dgetdrv() takes no arguments
    log_write(LOG_API, "Dgetdrv() -> %u", FIXED_DRIVE_CODE);
    return FIXED_DRIVE_CODE;
}

// [PARTIAL] Dgetpath(BYTE *buf, WORD drive). Always answers the root, since Dsetpath() is unimplemented and no subdirectory can ever become current.
unsigned int gemdos_dgetpath(unsigned int args_addr) {
    unsigned int buf_addr = m68k_read_memory_32(args_addr);
    int16_t drive = (int16_t)m68k_read_memory_16(args_addr + 4);

    guest_write_cstring(buf_addr, "\\", 128);

    log_write(LOG_API, "Dgetpath(drive=%d) -> '\\'", drive);
    return 0; // E_OK
}

// [FULL] Dcreate(BYTE *path)
unsigned int gemdos_dcreate(unsigned int args_addr) {
    unsigned int path_addr = m68k_read_memory_32(args_addr);
    char guest_path[260];
    guest_read_cstring(path_addr, guest_path, sizeof(guest_path));
    char path[260];
    gemdos_translate_path(guest_path, path, sizeof(path));

    BOOL ok = CreateDirectoryA(path, NULL);
    if (!ok && GetLastError() == ERROR_ALREADY_EXISTS) {
        ok = TRUE; // GEMDOS treats creating an already-existing directory as fine
    }

    log_write(LOG_API, "Dcreate('%s') -> %s", path, ok ? "CreateDirectoryA succeeded" : "CreateDirectoryA failed");
    return ok ? 0u : (unsigned int)EACCDN;
}