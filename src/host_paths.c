#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "host_paths.h"

void resolve_project_path(const char *relative_path, char *out, size_t out_size) {
    char exe_path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe_path, MAX_PATH);
    if (len == 0 || len == MAX_PATH) {
        snprintf(out, out_size, "%s", relative_path);
        return;
    }

    char *last_sep = strrchr(exe_path, '\\');
    if (last_sep) {
        *last_sep = '\0';
    }

    snprintf(out, out_size, "%s\\..\\..\\%s", exe_path, relative_path);
}

int chdir_to_program_dir(const char *prg_path, char *resolved_out, size_t resolved_out_size) {
    char abs_path[MAX_PATH];
    DWORD len = GetFullPathNameA(prg_path, MAX_PATH, abs_path, NULL);
    if (len == 0 || len >= MAX_PATH) {
        snprintf(resolved_out, resolved_out_size, "%s", prg_path);
        return 0;
    }

    snprintf(resolved_out, resolved_out_size, "%s", abs_path);

    char dir[MAX_PATH];
    strncpy(dir, abs_path, sizeof(dir) - 1);
    dir[sizeof(dir) - 1] = '\0';

    char *last_sep = strrchr(dir, '\\');
    if (!last_sep) {
        last_sep = strrchr(dir, '/');
    }
    if (!last_sep) {
        return 0;
    }
    *last_sep = '\0';

    if (!SetCurrentDirectoryA(dir)) {
        return 0;
    }

    return 1;
}

void gemdos_translate_path(const char *guest_path, char *out, size_t out_size) {
    const char *p = guest_path;
    if (p[0] != '\0' && p[1] == ':') {
        p += 2;
    }
    while (*p == '\\' || *p == '/') {
        p++;
    }
    if (*p == '\0') {
        p = ".";
    }
    snprintf(out, out_size, "%s", p);
}