#include "core/crash_diag.h"

#include "core/log.h"

#include <execinfo.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

static volatile sig_atomic_t g_phase = (sig_atomic_t)PHASE_UNKNOWN;
static volatile sig_atomic_t g_in_fatal = 0;

const char* crash_diag_phase_name(EnginePhase phase) {
	switch (phase) {
		case PHASE_UNKNOWN: return "PHASE_UNKNOWN";
		case PHASE_BOOT_SCENES_RUNNING: return "PHASE_BOOT_SCENES_RUNNING";
		case PHASE_SCENE_TO_MAP_REQUEST: return "PHASE_SCENE_TO_MAP_REQUEST";
		case PHASE_MAP_LOAD_BEGIN: return "PHASE_MAP_LOAD_BEGIN";
		case PHASE_MAP_ASSETS_LOAD: return "PHASE_MAP_ASSETS_LOAD";
		case PHASE_MAP_INIT_WORLD: return "PHASE_MAP_INIT_WORLD";
		case PHASE_MAP_SPAWN_ENTITIES_BEGIN: return "PHASE_MAP_SPAWN_ENTITIES_BEGIN";
		case PHASE_MAP_SPAWN_ENTITIES_END: return "PHASE_MAP_SPAWN_ENTITIES_END";
		case PHASE_AUDIO_TRACK_SWITCH_BEGIN: return "PHASE_AUDIO_TRACK_SWITCH_BEGIN";
		case PHASE_AUDIO_TRACK_SWITCH_END: return "PHASE_AUDIO_TRACK_SWITCH_END";
		case PHASE_FIRST_FRAME_RENDER: return "PHASE_FIRST_FRAME_RENDER";
		case PHASE_GAMEPLAY_UPDATE_TICK: return "PHASE_GAMEPLAY_UPDATE_TICK";
	}
	return "PHASE_UNKNOWN";
}

void crash_diag_set_phase(EnginePhase phase) {
	g_phase = (sig_atomic_t)phase;
}

EnginePhase crash_diag_phase(void) {
	return (EnginePhase)g_phase;
}

static void fd_write_all(int fd, const char* data, size_t len) {
	if (fd < 0 || !data || len == 0) {
		return;
	}
	while (len > 0) {
		ssize_t n = write(fd, data, len);
		if (n > 0) {
			data += (size_t)n;
			len -= (size_t)n;
			continue;
		}
		if (n < 0 && errno == EINTR) {
			continue;
		}
		break;
	}
}

static void fd_printf(int fd, const char* fmt, ...) {
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n <= 0) {
		return;
	}
	size_t len = (size_t)n;
	if (len >= sizeof(buf)) {
		len = sizeof(buf) - 1;
	}
	fd_write_all(fd, buf, len);
}

static void fatal_handler(int sig, siginfo_t* info, void* uctx) {
	(void)uctx;
	if (g_in_fatal) {
		// Re-entrancy: just exit.
		_exit(128 + sig);
	}
	g_in_fatal = 1;

	int fd = log_file_fd();
	if (fd < 0) {
		fd = STDERR_FILENO;
	}

	void* addr = info ? info->si_addr : NULL;
	EnginePhase phase = (EnginePhase)g_phase;
	fd_write_all(fd, "\n==================== FATAL ====================\n", 52);
	fd_printf(fd, "Signal: %d (%s)  FaultAddr: %p\n", sig, strsignal(sig), addr);
	fd_printf(fd, "Phase: %d (%s)\n", (int)phase, crash_diag_phase_name(phase));
	const char* lp = log_file_path();
	if (lp) {
		fd_printf(fd, "LogFile: %s\n", lp);
	}

	// Dump a backtrace. (Not strictly async-signal-safe, but extremely useful in practice.)
	fd_write_all(fd, "\n---- backtrace ----\n", 21);
	void* frames[128];
	int nframes = backtrace(frames, (int)(sizeof(frames) / sizeof(frames[0])));
	backtrace_symbols_fd(frames, nframes, fd);
	fd_write_all(fd, "---- end backtrace ----\n", 25);

	// Dump the last N log lines that led here.
	log_dump_ringbuffer_async(fd);

	// Best-effort flush.
	(void)fsync(fd);
	_exit(128 + sig);
}

static void install_one(int sig) {
	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = fatal_handler;
	sa.sa_flags = SA_SIGINFO | SA_RESETHAND;
	sigemptyset(&sa.sa_mask);
	(void)sigaction(sig, &sa, NULL);
}

void crash_diag_init(void) {
	// Phase starts unknown until main sets it.
	g_phase = (sig_atomic_t)PHASE_UNKNOWN;
	install_one(SIGSEGV);
	install_one(SIGABRT);
	install_one(SIGBUS);
	install_one(SIGILL);
	log_info_s("crash", "Crash diagnostics enabled (log=%s)", log_file_path() ? log_file_path() : "(no file sink)");
}
