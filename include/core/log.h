#pragma once

#include <stdbool.h>

#include <stddef.h>

typedef enum LogLevel {
	LOG_LEVEL_ERROR = 0,
	LOG_LEVEL_WARN = 1,
	LOG_LEVEL_INFO = 2,
	LOG_LEVEL_DEBUG = 3,
} LogLevel;

bool log_init(LogLevel level);
void log_shutdown(void);

// Path to the current log file (or NULL if no file sink is active).
const char* log_file_path(void);

// File descriptor for the current log file (or -1 if no file sink is active).
int log_file_fd(void);

// Async-signal-safe: dumps the in-memory log ring buffer to the given fd.
// Intended for fatal signal handlers.
void log_dump_ringbuffer_async(int fd);

void log_error(const char* fmt, ...);
void log_warn(const char* fmt, ...);
void log_info(const char* fmt, ...);
void log_debug(const char* fmt, ...);

// Optional subsystem variants.
void log_error_s(const char* subsystem, const char* fmt, ...);
void log_warn_s(const char* subsystem, const char* fmt, ...);
void log_info_s(const char* subsystem, const char* fmt, ...);
void log_debug_s(const char* subsystem, const char* fmt, ...);
