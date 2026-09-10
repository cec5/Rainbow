#include <stdlib.h>
#include <windows.h>
#include "aes/aes_rsrc.h"
#include "aes/aes_object.h"
#include "gemdos/gemdos_mem.h"
#include "guest_mem_util.h"
#include "host_paths.h"
#include "m68k.h"
#include "logger.h"

#define RSHDR_SIZE     36
#define TEDINFO_SIZE   28
#define ICONBLK_SIZE   34
#define BITBLK_SIZE    14

#define RSH_OFS_OBJECT   2
#define RSH_OFS_TEDINFO  4
#define RSH_OFS_ICONBLK  6
#define RSH_OFS_BITBLK   8
#define RSH_OFS_FRSTR    10
#define RSH_OFS_TRINDEX  18
#define RSH_OFS_NOBS     20
#define RSH_OFS_NTREE    22
#define RSH_OFS_NTED     24
#define RSH_OFS_NIB      26
#define RSH_OFS_NBB      28
#define RSH_OFS_NSTRING  30

// rsrc_gaddr() resource-part selectors
#define R_TREE    0
#define R_OBJECT  1
#define R_TEDINFO 2
#define R_ICONBLK 3
#define R_BITBLK  4
#define R_STRING  5

#define MAX_RSRC_TREES 128

#define GL_WCHAR 8
#define GL_HCHAR 16

typedef struct {
    int loaded;
    unsigned int base;          // guest address the whole file was copied to
    unsigned int object_addr;   // base + rsh_object
    unsigned int tedinfo_addr;  // base + rsh_tedinfo
    unsigned int iconblk_addr;  // base + rsh_iconblk
    unsigned int bitblk_addr;   // base + rsh_bitblk
    unsigned int frstr_addr;    // base + rsh_frstr
    int nobs, nted, nib, nbb, nstring, ntree;
    unsigned int tree_addr[MAX_RSRC_TREES];
} LoadedRsrc;

static LoadedRsrc s_rsrc = {0};

// FIELD HELPERS

static unsigned int rd_be16(const unsigned char *p) {
    return ((unsigned int)p[0] << 8) | p[1];
}

static void relocate_field(unsigned int addr, unsigned int base) {
    unsigned int v = m68k_read_memory_32(addr);
    if (v != 0) {
        m68k_write_memory_32(addr, base + v);
    }
}

static void obfix_field(unsigned int addr, int cell) {
    unsigned int packed = m68k_read_memory_16(addr);
    int fixed = (int)(packed & 0xFF) * cell + (int)((packed >> 8) & 0xFF);
    m68k_write_memory_16(addr, (unsigned int)(fixed & 0xFFFF));
}

static unsigned int clamp_count(unsigned int offset, unsigned int count, unsigned int record_size, DWORD file_size) {
    if ((unsigned long long)offset + (unsigned long long)count * record_size <= file_size) {
        return count;
    }
    return offset < file_size ? (file_size - offset) / record_size : 0;
}

static void obfix_object(unsigned int obj_addr) {
    obfix_field(obj_addr + 16, GL_WCHAR); // ob_x
    obfix_field(obj_addr + 18, GL_HCHAR); // ob_y
    obfix_field(obj_addr + 20, GL_WCHAR); // ob_w
    obfix_field(obj_addr + 22, GL_HCHAR); // ob_h
}

// LOADING

static void free_loaded_rsrc(void) {
    if (s_rsrc.loaded) {
        gemdos_mem_free(s_rsrc.base);
    }
    s_rsrc = (LoadedRsrc){0};
}

static int load_and_relocate(const char *fname) {
    char host_path[260];
    gemdos_translate_path(fname, host_path, sizeof(host_path));

    HANDLE h = CreateFileA(host_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 0;
    }

    DWORD file_size = GetFileSize(h, NULL);
    if (file_size == INVALID_FILE_SIZE || file_size < RSHDR_SIZE) {
        CloseHandle(h);
        return 0;
    }

    unsigned char *data = malloc(file_size);
    if (!data) {
        CloseHandle(h);
        return 0;
    }

    DWORD read = 0;
    BOOL ok = ReadFile(h, data, file_size, &read, NULL);
    CloseHandle(h);
    if (!ok || read != file_size) {
        free(data);
        return 0;
    }

    unsigned int rsh_object   = rd_be16(data + RSH_OFS_OBJECT);
    unsigned int rsh_tedinfo  = rd_be16(data + RSH_OFS_TEDINFO);
    unsigned int rsh_iconblk  = rd_be16(data + RSH_OFS_ICONBLK);
    unsigned int rsh_bitblk   = rd_be16(data + RSH_OFS_BITBLK);
    unsigned int rsh_frstr    = rd_be16(data + RSH_OFS_FRSTR);
    unsigned int rsh_trindex  = rd_be16(data + RSH_OFS_TRINDEX);
    unsigned int nobs         = rd_be16(data + RSH_OFS_NOBS);
    unsigned int ntree        = rd_be16(data + RSH_OFS_NTREE);
    unsigned int nted         = rd_be16(data + RSH_OFS_NTED);
    unsigned int nib          = rd_be16(data + RSH_OFS_NIB);
    unsigned int nbb          = rd_be16(data + RSH_OFS_NBB);
    unsigned int nstring      = rd_be16(data + RSH_OFS_NSTRING);

    nobs    = clamp_count(rsh_object,  nobs,    OBJECT_SIZE,  file_size);
    nted    = clamp_count(rsh_tedinfo, nted,    TEDINFO_SIZE, file_size);
    nib     = clamp_count(rsh_iconblk, nib,     ICONBLK_SIZE, file_size);
    nbb     = clamp_count(rsh_bitblk,  nbb,     BITBLK_SIZE,  file_size);
    nstring = clamp_count(rsh_frstr,   nstring, 4,            file_size);
    ntree   = clamp_count(rsh_trindex, ntree,   4,            file_size);

    if (ntree > MAX_RSRC_TREES) {
        ntree = MAX_RSRC_TREES;
    }

    unsigned int base = gemdos_mem_malloc((int32_t)file_size);
    if (base == 0) {
        free(data);
        return 0;
    }

    for (DWORD i = 0; i < file_size; i++) {
        m68k_write_memory_8(base + i, data[i]);
    }
    free(data);

    unsigned int object_addr  = base + rsh_object;
    unsigned int tedinfo_addr = base + rsh_tedinfo;
    unsigned int iconblk_addr = base + rsh_iconblk;
    unsigned int bitblk_addr  = base + rsh_bitblk;
    unsigned int frstr_addr   = base + rsh_frstr;

    for (unsigned int i = 0; i < nobs; i++) {
        unsigned int obj_addr = object_addr + i * OBJECT_SIZE;
        unsigned int type = m68k_read_memory_16(obj_addr + OBJECT_OFS_TYPE) & 0xFF;

        obfix_object(obj_addr);

        switch (type) {
            case G_BOX:
            case G_IBOX:
            case G_BOXCHAR:
            case G_PROGDEF:
                break;

            default:
                relocate_field(obj_addr + 12, base);
                break;
        }
    }

    for (unsigned int i = 0; i < nted; i++) {
        unsigned int ted_addr = tedinfo_addr + i * TEDINFO_SIZE;
        relocate_field(ted_addr + 0, base);  // te_ptext
        relocate_field(ted_addr + 4, base);  // te_ptmplt
        relocate_field(ted_addr + 8, base);  // te_pvalid
    }

    for (unsigned int i = 0; i < nib; i++) {
        unsigned int ib_addr = iconblk_addr + i * ICONBLK_SIZE;
        relocate_field(ib_addr + 0, base);   // ib_pmask
        relocate_field(ib_addr + 4, base);   // ib_pdata
        relocate_field(ib_addr + 8, base);   // ib_ptext
    }

    for (unsigned int i = 0; i < nbb; i++) {
        relocate_field(bitblk_addr + i * BITBLK_SIZE, base);  // bi_pdata
    }

    // Free-string table: LONGs at rsh_frstr, each a file-relative offset into the string pool; used by rsrc_gaddr(R_STRING, index).
    for (unsigned int i = 0; i < nstring; i++) {
        relocate_field(frstr_addr + i * 4, base);
    }

    free_loaded_rsrc(); // release whatever resource was loaded before this one

    s_rsrc.loaded = 1;
    s_rsrc.base = base;
    s_rsrc.object_addr = object_addr;
    s_rsrc.tedinfo_addr = tedinfo_addr;
    s_rsrc.iconblk_addr = iconblk_addr;
    s_rsrc.bitblk_addr = bitblk_addr;
    s_rsrc.frstr_addr = frstr_addr;
    s_rsrc.nobs = (int)nobs;
    s_rsrc.nted = (int)nted;
    s_rsrc.nib = (int)nib;
    s_rsrc.nbb = (int)nbb;
    s_rsrc.nstring = (int)nstring;
    s_rsrc.ntree = (int)ntree;

    for (unsigned int i = 0; i < ntree; i++) {
        unsigned int file_ofs = m68k_read_memory_32(base + rsh_trindex + i * 4);
        int in_bounds = file_ofs >= rsh_object &&
                         (file_ofs - rsh_object) % OBJECT_SIZE == 0 &&
                         (file_ofs - rsh_object) / OBJECT_SIZE < nobs;
        s_rsrc.tree_addr[i] = in_bounds ? base + file_ofs : 0;
    }

    return 1;
}

// AES ENTRY POINTS

// [FULL] Loading also relocates the file's internal offsets into guest addresses, which is what makes the tree walkable afterwards.
void aes_rsrc_load(const AesPB *pb) {
    unsigned int fname_addr = aes_pb_addrin(pb, 0);
    char fname[260];
    guest_read_cstring(fname_addr, fname, sizeof(fname));

    int ok = load_and_relocate(fname);

    aes_pb_set_intout(pb, 0, (int16_t)ok);
    if (ok) {
        log_write(LOG_API, "rsrc_load(\"%s\") -> loaded at 0x%08X (%d objects, %d trees, %d tedinfo, %d iconblk, %d bitblk, %d strings)", fname, s_rsrc.base, s_rsrc.nobs, s_rsrc.ntree, s_rsrc.nted, s_rsrc.nib, s_rsrc.nbb, s_rsrc.nstring);
    } else {
        log_write(LOG_API, "rsrc_load(\"%s\") -> failed (missing, unreadable, or too small to be a valid .RSC)", fname);
    }
}

// [PARTIAL] Six of the resource types are addressable; any other type falls through the switch and is reported as not found.
void aes_rsrc_gaddr(const AesPB *pb) {
    int16_t type = aes_pb_intin(pb, 0);
    int16_t index = aes_pb_intin(pb, 1);

    unsigned int addr = 0;
    int ok = 0;

    if (s_rsrc.loaded && index >= 0) {
        switch (type) {
            case R_TREE:
                if (index < s_rsrc.ntree && s_rsrc.tree_addr[index] != 0) {
                    addr = s_rsrc.tree_addr[index];
                    ok = 1;
                }
                break;
            case R_OBJECT:
                if (index < s_rsrc.nobs) {
                    addr = s_rsrc.object_addr + (unsigned int)index * OBJECT_SIZE;
                    ok = 1;
                }
                break;
            case R_TEDINFO:
                if (index < s_rsrc.nted) {
                    addr = s_rsrc.tedinfo_addr + (unsigned int)index * TEDINFO_SIZE;
                    ok = 1;
                }
                break;
            case R_ICONBLK:
                if (index < s_rsrc.nib) {
                    addr = s_rsrc.iconblk_addr + (unsigned int)index * ICONBLK_SIZE;
                    ok = 1;
                }
                break;
            case R_BITBLK:
                if (index < s_rsrc.nbb) {
                    addr = s_rsrc.bitblk_addr + (unsigned int)index * BITBLK_SIZE;
                    ok = 1;
                }
                break;
            case R_STRING:
                if (index < s_rsrc.nstring) {
                    addr = m68k_read_memory_32(s_rsrc.frstr_addr + (unsigned int)index * 4);
                    ok = 1;
                }
                break;
            default:
                break;
        }
    }

    aes_pb_set_intout(pb, 0, (int16_t)ok);
    aes_pb_set_addrout(pb, 0, addr);

    log_write(LOG_API, "rsrc_gaddr(type=%d, index=%d) -> %s 0x%08X", type, index, ok ? "found" : "not found", addr);
}

// [FULL] rsrc_free()
void aes_rsrc_free(const AesPB *pb) {
    int was_loaded = s_rsrc.loaded;
    free_loaded_rsrc();

    aes_pb_set_intout(pb, 0, (int16_t)(was_loaded ? 1 : 0));
    log_write(LOG_API, "rsrc_free() -> %s", was_loaded ? "resource released" : "nothing was loaded");
}

// SHARED ACCESSORS

unsigned int aes_rsrc_object_base(void) {
    return s_rsrc.loaded ? s_rsrc.object_addr : 0;
}

int aes_rsrc_object_count(void) {
    return s_rsrc.loaded ? s_rsrc.nobs : 0;
}