#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <windows.h>
#include "logger.h"
#include "host_paths.h"

#define LOG_DIR "logs"
#define MAX_LOG_PATH 512

static FILE *s_log_file = NULL;
static unsigned long s_seq = 0;
static char s_log_path[MAX_LOG_PATH];

static const char *category_name(LogCategory category) {
    switch (category) {
        case LOG_INFO:  return "INFO";
        case LOG_TRAP:  return "TRAP";
        case LOG_API:   return "API";
        case LOG_ERROR: return "ERROR";
        default:        return "?";
    }
}

static void extract_basename(const char *path, char *out, size_t out_size) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '\\' || *p == '/') {
            base = p + 1;
        }
    }

    const char *last_dot = NULL;
    for (const char *p = base; *p; p++) {
        if (*p == '.') {
            last_dot = p;
        }
    }

    size_t copy_len = last_dot ? (size_t)(last_dot - base) : strlen(base);
    if (copy_len >= out_size) {
        copy_len = out_size - 1;
    }
    memcpy(out, base, copy_len);
    out[copy_len] = '\0';

    if (out[0] == '\0') {
        strncpy(out, "program", out_size - 1);
        out[out_size - 1] = '\0';
    }
}

void logger_init_for_program(const char *program_path) {
    char log_dir[MAX_LOG_PATH];
    resolve_project_path(LOG_DIR, log_dir, sizeof(log_dir));

    if (!CreateDirectoryA(log_dir, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {
        fprintf(stderr, "[logger] Could not create log directory '%s' (error %lu) - debug logging disabled\n", log_dir, (unsigned long)GetLastError());
        return;
    }

    char basename[128];
    extract_basename(program_path, basename, sizeof(basename));

    SYSTEMTIME t;
    GetLocalTime(&t);

    snprintf(s_log_path, sizeof(s_log_path), "%s\\%s_%04d%02d%02d_%02d%02d%02d.log", log_dir, basename, t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);

    s_log_file = fopen(s_log_path, "w");
    if (!s_log_file) {
        fprintf(stderr, "[logger] Could not open log file '%s' - debug logging disabled\n", s_log_path);
        s_log_path[0] = '\0';
        return;
    }

    fprintf(s_log_file, "# Rainbow debug log for '%s'\n", program_path);
    fprintf(s_log_file, "# [seq] [HH:MM:SS.mmm] [CATEGORY] message\n\n");
    fflush(s_log_file);
}

const char *logger_get_log_path(void) {
    return s_log_file ? s_log_path : NULL;
}

int logger_is_enabled(void) {
    return s_log_file != NULL;
}

void logger_shutdown(void) {
    if (s_log_file) {
        fclose(s_log_file);
        s_log_file = NULL;
    }
}

void log_write(LogCategory category, const char *fmt, ...) {
    if (!s_log_file) {
        return;
    }

    SYSTEMTIME t;
    GetLocalTime(&t);

    fprintf(s_log_file, "[%06lu] [%02d:%02d:%02d.%03d] [%-5s] ", ++s_seq, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, category_name(category));

    va_list args;
    va_start(args, fmt);
    vfprintf(s_log_file, fmt, args);
    va_end(args);

    fprintf(s_log_file, "\n");
    fflush(s_log_file);
}