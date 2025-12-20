#include "core/log.h"

#include <stdarg.h>
#include <stdio.h>

static LogLevel g_level = LOG_LEVEL_INFO;

bool log_init(LogLevel level) {
	g_level = level;
	return true;
}

void log_shutdown(void) {
	// no-op for now
}

static void log_v(LogLevel lvl, const char* tag, const char* fmt, va_list ap) {
	if (lvl > g_level) {
		return;
	}
	fprintf(stderr, "%s: ", tag);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
}

void log_error(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v(LOG_LEVEL_ERROR, "ERROR", fmt, ap);
	va_end(ap);
}

void log_warn(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v(LOG_LEVEL_WARN, "WARN", fmt, ap);
	va_end(ap);
}

void log_info(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v(LOG_LEVEL_INFO, "INFO", fmt, ap);
	va_end(ap);
}

void log_debug(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v(LOG_LEVEL_DEBUG, "DEBUG", fmt, ap);
	va_end(ap);
}
