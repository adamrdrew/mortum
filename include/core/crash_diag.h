#pragma once

#include <signal.h>

// High-signal phase markers for isolating crashes around scene->map transitions.
// Keep this list stable and append-only for easier log comparison.
typedef enum EnginePhase {
	PHASE_UNKNOWN = 0,
	PHASE_BOOT_SCENES_RUNNING,
	PHASE_SCENE_TO_MAP_REQUEST,
	PHASE_MAP_LOAD_BEGIN,
	PHASE_MAP_ASSETS_LOAD,
	PHASE_MAP_INIT_WORLD,
	PHASE_MAP_SPAWN_ENTITIES_BEGIN,
	PHASE_MAP_SPAWN_ENTITIES_END,
	PHASE_AUDIO_TRACK_SWITCH_BEGIN,
	PHASE_AUDIO_TRACK_SWITCH_END,
	PHASE_FIRST_FRAME_RENDER,
	PHASE_GAMEPLAY_UPDATE_TICK,
} EnginePhase;

// Installs fatal signal handlers and initializes crash diagnostics.
// Call early (after log_init is fine; it will use the log file sink if present).
void crash_diag_init(void);

void crash_diag_set_phase(EnginePhase phase);
EnginePhase crash_diag_phase(void);
const char* crash_diag_phase_name(EnginePhase phase);
