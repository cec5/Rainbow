#ifndef HOST_PATHS_H
#define HOST_PATHS_H

#include <stddef.h>

void resolve_project_path(const char *relative_path, char *out, size_t out_size);

int chdir_to_program_dir(const char *prg_path, char *resolved_out, size_t resolved_out_size);

void gemdos_translate_path(const char *guest_path, char *out, size_t out_size);

#endif