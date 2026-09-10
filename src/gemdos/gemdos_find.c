#include <stdint.h>
#include <windows.h>
#include "gemdos/gemdos_find.h"
#include "gemdos/gemdos_errno.h"
#include "m68k.h"
#include "guest_mem_util.h"
#include "host_paths.h"
#include "logger.h"

#define DTA_ATTRIB_OFF 21
#define DTA_TIME_OFF   22
#define DTA_DATE_OFF   24
#define DTA_LENGTH_OFF 26
#define DTA_FNAME_OFF  30
#define DTA_FNAME_LEN  14

#define FA_READONLY 0x01
#define FA_HIDDEN   0x02
#define FA_SYSTEM   0x04
#define FA_DIR      0x10

// Aliases the basepage's command-line field at the fixed load address main.c always uses. Real programs call Fsetdta() before relying on the DTA, so this only has to be somewhere harmless.
#define DEFAULT_DTA_ADDR 0x00001080u

static unsigned int s_dta_addr = DEFAULT_DTA_ADDR;

// DTA STATE

// [FULL] Fsetdta(DTA *buf)
unsigned int gemdos_fsetdta(unsigned int args_addr) {
    unsigned int ndta = m68k_read_memory_32(args_addr);
    s_dta_addr = ndta;
    log_write(LOG_API, "Fsetdta(0x%08X)", ndta);
    return 0;
}

// [FULL] Fgetdta()
unsigned int gemdos_fgetdta(unsigned int args_addr) {
    (void)args_addr;
    log_write(LOG_API, "Fgetdta() -> 0x%08X", s_dta_addr);
    return s_dta_addr;
}

// FSFIRST

static unsigned char win32_attrib_to_gemdos(DWORD attr) {
    unsigned char out = 0;
    if (attr & FILE_ATTRIBUTE_READONLY)  out |= FA_READONLY;
    if (attr & FILE_ATTRIBUTE_HIDDEN)    out |= FA_HIDDEN;
    if (attr & FILE_ATTRIBUTE_SYSTEM)    out |= FA_SYSTEM;
    if (attr & FILE_ATTRIBUTE_DIRECTORY) out |= FA_DIR;
    return out;
}

static void fill_dta(const WIN32_FIND_DATAA *fd) {
    unsigned char attrib = win32_attrib_to_gemdos(fd->dwFileAttributes);
    m68k_write_memory_8(s_dta_addr + DTA_ATTRIB_OFF, attrib);

    WORD dos_date = 0, dos_time = 0;
    FileTimeToDosDateTime(&fd->ftLastWriteTime, &dos_date, &dos_time);
    m68k_write_memory_16(s_dta_addr + DTA_TIME_OFF, dos_time);
    m68k_write_memory_16(s_dta_addr + DTA_DATE_OFF, dos_date);

    m68k_write_memory_32(s_dta_addr + DTA_LENGTH_OFF, fd->nFileSizeLow);

    // Prefer the 8.3 short name for non-compliant names, since d_fname only holds 13 chars plus a NUL.
    const char *name = (fd->cAlternateFileName[0] != '\0') ? fd->cAlternateFileName : fd->cFileName;

    for (int i = 0; i < DTA_FNAME_LEN; i++) {
        m68k_write_memory_8(s_dta_addr + DTA_FNAME_OFF + i, 0);
    }
    guest_write_cstring(s_dta_addr + DTA_FNAME_OFF, name, DTA_FNAME_LEN);
}

// [PARTIAL] Only the first match is ever reachable, because Fsnext() is unimplemented.
unsigned int gemdos_fsfirst(unsigned int args_addr) {
    unsigned int fspec_addr = m68k_read_memory_32(args_addr);
    int16_t attribs = (int16_t)m68k_read_memory_16(args_addr + 4);

    char guest_fspec[1024];
    guest_read_cstring(fspec_addr, guest_fspec, sizeof(guest_fspec));
    char fspec[1024];
    gemdos_translate_path(guest_fspec, fspec, sizeof(fspec));

    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(fspec, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        log_write(LOG_ERROR, "Fsfirst('%s', attribs=0x%02X) -> FindFirstFileA failed, GetLastError=%lu", fspec, (unsigned int)(uint16_t)attribs, (unsigned long)GetLastError());
        return (unsigned int)EFILNF;
    }

    // FindFirstFileA cannot apply GEMDOS's attribs filter, so keep asking for entries until one passes it. Running out leaves matched clear.
    int matched;
    do {
        int is_dir    = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        int is_hidden = (fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
        int is_system = (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;

        matched = 1;
        if (is_dir    && !(attribs & FA_DIR))    matched = 0;
        if (is_hidden && !(attribs & FA_HIDDEN)) matched = 0;
        if (is_system && !(attribs & FA_SYSTEM)) matched = 0;
    } while (!matched && FindNextFileA(h, &fd));

    if (!matched) {
        log_write(LOG_ERROR, "Fsfirst('%s', attribs=0x%02X) -> no matching entry", fspec, (unsigned int)(uint16_t)attribs);
        FindClose(h);
        return (unsigned int)EFILNF;
    }

    fill_dta(&fd);
    FindClose(h);

    log_write(LOG_API, "Fsfirst('%s', attribs=0x%02X) -> '%s' into DTA@0x%08X " "(Fsnext() isn't implemented yet, so only this first match is reachable)", fspec, (unsigned int)(uint16_t)attribs, fd.cFileName, s_dta_addr);
    return 0;
}