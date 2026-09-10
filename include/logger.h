#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_INFO,  // lifecycle events: startup, program loaded, halted, etc.
    LOG_TRAP,  // a TRAP #1/#13/#14 fired and was intercepted
    LOG_API,   // a TOS API call was serviced, and what it mapped to on Win32
    LOG_ERROR  // a call failed, or hit something unimplemented
} LogCategory;


void logger_init_for_program(const char *program_path);

const char *logger_get_log_path(void);

int logger_is_enabled(void);

void logger_shutdown(void);

void log_write(LogCategory category, const char *fmt, ...);

#endif