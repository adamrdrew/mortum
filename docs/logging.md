# Logging and Crash Diagnostics (Mortum)

This document describes Mortum’s always-on logging and crash diagnostics infrastructure, with emphasis on **map start / scene→map transitions** and **non-deterministic crashes**.

## Quick summary

- All logs go to **stderr** *and* to a persistent log file.
- Logging is **unbuffered** so tail logs aren’t lost on crashes.
- A **ring buffer** keeps the last N log lines in memory; on a fatal signal, it is dumped to the log file.
- Fatal signal handler prints:
	- signal + fault address
	- last transition **phase marker**
	- stack trace via `backtrace()` / `backtrace_symbols_fd()`
	- ring-buffered last log lines

## Where the log file is

Mortum writes a deterministic log file named `mortum.log` in the OS temp directory.

- On macOS, this resolves from `TMPDIR` (if set), otherwise `/tmp`.
- The resolved path is printed at startup:
	- `Crash diagnostics enabled (log=/…/mortum.log)`

Example path (macOS):

- `/var/folders/.../T/mortum.log`

## Log format

Every line is formatted like:

- `YYYY-MM-DD HH:MM:SS.mmm [tid=<thread>] <LEVEL> <SUBSYSTEM> <message>`

Where:

- `LEVEL` is one of: `ERROR`, `WARN`, `INFO`, `DEBUG`
- `SUBSYSTEM` is a short tag (examples: `GEN`, `crash`, `screen`, `transition`, `music`, `midi`, `map`)
- `tid` is a stable numeric thread id (useful when multi-threaded subsystems are involved)

## How to use it

### Repro + capture

1. Run as normal (`make run`).
2. If the process crashes, open the log file printed at startup and search for:
	- `==================== FATAL ====================`

That section includes:

- signal and fault address
- the last `EnginePhase`
- a backtrace
- the last N log lines (ring buffer)

### Finding the failure phase

Mortum maintains a global “phase marker” for high-signal transition localization.

Phases are defined in:

- `include/core/crash_diag.h` (`EnginePhase`)

The crash handler prints:

- `Phase: <id> (<name>)`

Typical map-start related phases include:

- `PHASE_SCENE_TO_MAP_REQUEST`
- `PHASE_MAP_LOAD_BEGIN`
- `PHASE_MAP_ASSETS_LOAD`
- `PHASE_MAP_INIT_WORLD`
- `PHASE_MAP_SPAWN_ENTITIES_BEGIN`
- `PHASE_MAP_SPAWN_ENTITIES_END`
- `PHASE_AUDIO_TRACK_SWITCH_BEGIN`
- `PHASE_AUDIO_TRACK_SWITCH_END`
- `PHASE_FIRST_FRAME_RENDER`
- `PHASE_GAMEPLAY_UPDATE_TICK`

### Getting actionable stacks

Mortum is built with debug info in the default `make`/`make run` configuration (`-g -O0`), so backtraces typically include function names.

If you want even higher-fidelity symbols:

- Ensure you’re running the binary you just built (`build/mortum`).
- Re-run on a clean build (`make clean && make`).

## Ring buffer (“last N lines”)

Mortum keeps the last `LOG_RING_LINES` log lines in memory.

- Current size: 256 lines
- Dumped on fatal signals into the log file

This is intended to preserve the final few operations even if the crash occurs in a context where stdio output could be lost.

Implementation:

- `src/core/log.c` maintains the ring buffer.
- Fatal handler calls `log_dump_ringbuffer_async(fd)`.

## Fatal signal handling

Fatal signals handled:

- `SIGSEGV`, `SIGABRT`, `SIGBUS`, `SIGILL`

On fatal:

1. Print signal + fault address
2. Print current `EnginePhase`
3. Print a backtrace
4. Dump the ring buffer
5. Flush and `_exit(128 + sig)`

Entry point:

- `crash_diag_init()` in `src/core/crash_diag.c`

Notes:

- The handler is designed to be “best-effort” and avoid unsafe unwinding.
- Backtrace collection is extremely useful; while not strictly async-signal-safe, it is typically worth it for crash diagnosis.

## Adding new logs

### Simple logs

Use the existing severity helpers:

- `log_error(...)`, `log_warn(...)`, `log_info(...)`, `log_debug(...)`

### Subsystem-tagged logs

Prefer the subsystem variants for high-signal diagnostics:

- `log_error_s("subsystem", ...)`
- `log_warn_s("subsystem", ...)`
- `log_info_s("subsystem", ...)`
- `log_debug_s("subsystem", ...)`

Suggested subsystem tags:

- `transition` for scene→map boundaries
- `map` for map load/destroy and world pointer dumps
- `screen` for screen runtime lifecycle
- `music` / `midi` for MIDI start/stop/init/shutdown
- `render` for render-time invariants and pointer checks

## Adding new phase markers

Phase markers should be set at **transition edges**, not in tight inner loops.

Rules of thumb:

- Use them to bracket “one-time” operations: map load, world init, spawn, audio switches.
- Keep phase changes sparse and meaningful.
- Prefer phases that answer: “what subsystem is currently doing work?”

API:

- `crash_diag_set_phase(PHASE_...)`

## Interpreting common crash patterns

### Crash in renderer with `texture_sample_nearest` on first frame

Strongly suggests a bad texture pointer or metadata during rendering.

Typical causes to check:

- invalidated pointers returned from caching registries
- loading a new resource mid-frame invalidating previously cached pointers
- invalid texture sizes / null pixel pointers

### Crash inside audio / FluidSynth callbacks

Likely an audio-thread race or lifecycle issue.

- Confirm stop/destroy ordering
- Ensure init is idempotent and teardown is safe
- Avoid freeing state used by callbacks without synchronization

## Relevant code locations

- Logging core: `src/core/log.c`, `include/core/log.h`
- Crash diagnostics: `src/core/crash_diag.c`, `include/core/crash_diag.h`
- Phase markers are set primarily in:
	- `src/main.c`
	- `src/game/timeline_flow.c`
	- `src/game/map_music.c`

## FAQ

### “I launched the app from Finder and saw no logs.”

Check the temp log file (`mortum.log`). Logging is not terminal-dependent.

### “Why is the log file overwritten each run?”

The log file is intentionally truncated on startup to make each run self-contained.

### “Can I increase ring buffer size?”

Yes: adjust `LOG_RING_LINES` / `LOG_RING_LINE_MAX` in `src/core/log.c`.
