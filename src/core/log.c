#include "core/log.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

static LogLevel g_level = LOG_LEVEL_INFO;

#define LOG_RING_LINES 256
#define LOG_RING_LINE_MAX 1024

typedef struct LogRingLine {
	char buf[LOG_RING_LINE_MAX];
	unsigned short len;
} LogRingLine;

static LogRingLine g_ring[LOG_RING_LINES];
static volatile sig_atomic_t g_ring_head = 0; // next write index
static volatile sig_atomic_t g_ring_count = 0; // number of valid lines (<= LOG_RING_LINES)

static int g_log_fd = -1;
static char g_log_path[512] = {0};

static unsigned long long log_thread_id_u64(void) {
#if defined(__APPLE__)
	// Stable numeric thread id for diagnostics.
	return (unsigned long long)pthread_mach_thread_np(pthread_self());
#else
	return (unsigned long long)(uintptr_t)pthread_self();
#endif
}

static void log_ring_push(const char* line, size_t len) {
	if (!line) {
		return;
	}
	if (len >= LOG_RING_LINE_MAX) {
		len = LOG_RING_LINE_MAX - 1;
	}
	int idx = (int)(g_ring_head % LOG_RING_LINES);
	memcpy(g_ring[idx].buf, line, len);
	g_ring[idx].buf[len] = '\0';
	g_ring[idx].len = (unsigned short)len;
	g_ring_head++;
	if (g_ring_count < LOG_RING_LINES) {
		g_ring_count++;
	}
}

static void log_write_fd(int fd, const char* data, size_t len) {
	if (fd < 0 || !data || len == 0) {
		return;
	}
	// Best-effort; avoid stdio buffering.
	while (len > 0) {
		ssize_t n = write(fd, data, len);
		if (n > 0) {
			data += (size_t)n;
			len -= (size_t)n;
			continue;
		}
		if (n < 0 && (errno == EINTR)) {
			continue;
		}
		break;
	}
}

static const char* log_level_tag(LogLevel lvl) {
	switch (lvl) {
		case LOG_LEVEL_ERROR: return "ERROR";
		case LOG_LEVEL_WARN: return "WARN";
		case LOG_LEVEL_INFO: return "INFO";
		case LOG_LEVEL_DEBUG: return "DEBUG";
	}
	return "LOG";
}

static void log_format_prefix(char* out, size_t out_cap, LogLevel lvl, const char* subsystem) {
	if (!out || out_cap == 0) {
		return;
	}
	struct timeval tv;
	gettimeofday(&tv, NULL);
	time_t sec = (time_t)tv.tv_sec;
	struct tm tmv;
	localtime_r(&sec, &tmv);
	int ms = (int)(tv.tv_usec / 1000);
	unsigned long long tid = log_thread_id_u64();
	const char* subsys = (subsystem && subsystem[0] != '\0') ? subsystem : "GEN";
	(void)snprintf(
		out,
		out_cap,
		"%04d-%02d-%02d %02d:%02d:%02d.%03d [tid=%llu] %-5s %-8s ",
		tmv.tm_year + 1900,
		tmv.tm_mon + 1,
		tmv.tm_mday,
		tmv.tm_hour,
		tmv.tm_min,
		tmv.tm_sec,
		ms,
		tid,
		log_level_tag(lvl),
		subsys
	);
}

bool log_init(LogLevel level) {
	g_level = level;
	// Ensure unbuffered output so we don't lose tail logs on crash.
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	// Create a deterministic file sink in a writable location.
	// Prefer TMPDIR on macOS; fall back to /tmp.
	const char* tmp = getenv("TMPDIR");
	if (!tmp || tmp[0] == '\0') {
		tmp = "/tmp";
	}
	// TMPDIR often ends with a trailing '/'; normalize so we don't produce "//mortum.log".
	char tmp_norm[384];
	(void)snprintf(tmp_norm, sizeof(tmp_norm), "%s", tmp);
	size_t n = strlen(tmp_norm);
	while (n > 1 && tmp_norm[n - 1] == '/') {
		tmp_norm[n - 1] = '\0';
		n--;
	}
	(void)snprintf(g_log_path, sizeof(g_log_path), "%s/%s", tmp_norm, "mortum.log");
	int fd = open(g_log_path, O_CREAT | O_TRUNC | O_WRONLY, (mode_t)0644);
	if (fd >= 0) {
		g_log_fd = fd;
	} else {
		g_log_fd = -1;
		g_log_path[0] = '\0';
	}
	return true;
}

void log_shutdown(void) {
	if (g_log_fd >= 0) {
		(void)fsync(g_log_fd);
		(void)close(g_log_fd);
		g_log_fd = -1;
	}
	g_log_path[0] = '\0';
}

const char* log_file_path(void) {
	return (g_log_path[0] != '\0') ? g_log_path : NULL;
}

int log_file_fd(void) {
	return g_log_fd;
}

static void log_v2(LogLevel lvl, const char* subsystem, const char* fmt, va_list ap) {
	if (lvl > g_level) {
		return;
	}
	char prefix[256];
	log_format_prefix(prefix, sizeof(prefix), lvl, subsystem);

	char msg[1536];
	int n0 = snprintf(msg, sizeof(msg), "%s", prefix);
	if (n0 < 0) {
		return;
	}
	if ((size_t)n0 >= sizeof(msg)) {
		n0 = (int)sizeof(msg) - 1;
		msg[n0] = '\0';
	}
	int n1 = vsnprintf(msg + n0, sizeof(msg) - (size_t)n0, fmt, ap);
	if (n1 < 0) {
		n1 = 0;
	}
	size_t used = (size_t)n0 + (size_t)n1;
	if (used >= sizeof(msg)) {
		used = sizeof(msg) - 1;
	}
	// Append newline.
	if (used + 1 < sizeof(msg)) {
		msg[used++] = '\n';
	}
	msg[used] = '\0';

	log_ring_push(msg, used);
	log_write_fd(STDERR_FILENO, msg, used);
	log_write_fd(g_log_fd, msg, used);
}

void log_error(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_ERROR, "GEN", fmt, ap);
	va_end(ap);
}

void log_warn(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_WARN, "GEN", fmt, ap);
	va_end(ap);
}

void log_info(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_INFO, "GEN", fmt, ap);
	va_end(ap);
}

void log_debug(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_DEBUG, "GEN", fmt, ap);
	va_end(ap);
}

void log_error_s(const char* subsystem, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_ERROR, subsystem, fmt, ap);
	va_end(ap);
}

void log_warn_s(const char* subsystem, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_WARN, subsystem, fmt, ap);
	va_end(ap);
}

void log_info_s(const char* subsystem, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_INFO, subsystem, fmt, ap);
	va_end(ap);
}

void log_debug_s(const char* subsystem, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	log_v2(LOG_LEVEL_DEBUG, subsystem, fmt, ap);
	va_end(ap);
}

void log_dump_ringbuffer_async(int fd) {
	if (fd < 0) {
		return;
	}
	// Snapshot indices (best-effort; may race with writers).
	sig_atomic_t count = g_ring_count;
	sig_atomic_t head = g_ring_head;
	if (count <= 0) {
		return;
	}
	const char* header = "\n---- last log lines (ring buffer) ----\n";
	log_write_fd(fd, header, strlen(header));
	int start = (int)(head - count);
	for (int i = 0; i < (int)count; i++) {
		int idx = (start + i);
		if (idx < 0) {
			idx = (LOG_RING_LINES + (idx % LOG_RING_LINES)) % LOG_RING_LINES;
		} else {
			idx = idx % LOG_RING_LINES;
		}
		unsigned short len = g_ring[idx].len;
		if (len > 0) {
			log_write_fd(fd, g_ring[idx].buf, (size_t)len);
			// Ensure newline if the stored line was truncated without one.
			if (g_ring[idx].buf[len - 1] != '\n') {
				log_write_fd(fd, "\n", 1);
			}
		}
	}
	const char* footer = "---- end ring buffer ----\n";
	log_write_fd(fd, footer, strlen(footer));
}
