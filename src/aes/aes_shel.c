#include <string.h>
#include <windows.h>
#include "aes/aes_shel.h"
#include "guest_mem_util.h"
#include "host_paths.h"
#include "logger.h"

static char s_scrap_path[128] = "C:\\CLIPBRD\\";

// SHELL

// [PARTIAL] Answers with an empty command and tail. Nothing launches the guest with arguments, so there is never anything to report.
void aes_shel_read(const AesPB *pb) {
    unsigned int cmd_addr = aes_pb_addrin(pb, 0);
    unsigned int tail_addr = aes_pb_addrin(pb, 1);

    guest_write_cstring(cmd_addr, "", 128);
    guest_write_cstring(tail_addr, "", 128);

    aes_pb_set_intout(pb, 0, 1);
    log_write(LOG_API, "shel_read() -> empty cmd/tail (command-line passthrough not modeled)");
}

// [FULL] The program's own directory is the whole search path here, so one lookup covers what real GEM would search several places for.
void aes_shel_find(const AesPB *pb) {
    unsigned int buf_addr = aes_pb_addrin(pb, 0);
    char name[260];
    guest_read_cstring(buf_addr, name, sizeof(name));

    char host_path[260];
    gemdos_translate_path(name, host_path, sizeof(host_path));

    DWORD attrs = GetFileAttributesA(host_path);
    int ok = (attrs != INVALID_FILE_ATTRIBUTES) && !(attrs & FILE_ATTRIBUTE_DIRECTORY);

    aes_pb_set_intout(pb, 0, (int16_t)ok);
    log_write(LOG_API, "shel_find(\"%s\") -> %s", name, ok ? "found in program directory" : "not found");
}

// SCRAP (CLIPBOARD)

// [FULL] The directory is created on demand, because a guest that asks for the scrap path expects to be able to write there immediately.
void aes_scrp_read(const AesPB *pb) {
    unsigned int buf_addr = aes_pb_addrin(pb, 0);

    char host_path[260];
    gemdos_translate_path(s_scrap_path, host_path, sizeof(host_path));
    CreateDirectoryA(host_path, NULL); // no-op if it already exists

    guest_write_cstring(buf_addr, s_scrap_path, 128);

    aes_pb_set_intout(pb, 0, 1);
    log_write(LOG_API, "scrp_read() -> \"%s\" (host \"%s\")", s_scrap_path, host_path);
}

// [FULL] scrp_write(). Reports whether the path the guest chose actually exists rather than creating it.
void aes_scrp_write(const AesPB *pb) {
    unsigned int buf_addr = aes_pb_addrin(pb, 0);
    guest_read_cstring(buf_addr, s_scrap_path, sizeof(s_scrap_path));

    char host_path[260];
    gemdos_translate_path(s_scrap_path, host_path, sizeof(host_path));

    DWORD attrs = GetFileAttributesA(host_path);
    int exists = (attrs != INVALID_FILE_ATTRIBUTES) && (attrs & FILE_ATTRIBUTE_DIRECTORY);

    aes_pb_set_intout(pb, 0, (int16_t)exists);
    log_write(LOG_API, "scrp_write(\"%s\") -> %s", s_scrap_path, exists ? "exists" : "does not exist");
}