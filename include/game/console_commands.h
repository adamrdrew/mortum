#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "assets/asset_paths.h"
#include "assets/timeline_loader.h"
#include "assets/map_loader.h"

#include "core/config.h"

#include "game/console.h"
#include "game/entities.h"
#include "game/timeline_flow.h"
#include "game/game_state.h"
#include "game/perf_trace.h"
#include "game/player.h"
#include "game/sound_emitters.h"

#include "game/screen_runtime.h"

#include "platform/window.h"

#include "render/framebuffer.h"
#include "render/level_mesh.h"

// Command wiring context. Commands call into engine systems through pointers here.
// Keep this POD and owned by main.

typedef struct ConsoleCommandContext {
	// Main loop control (optional).
	bool* running;

	// Deferred command execution (used by MenuScreen). Executed by main at a safe point.
	char deferred_line[CONSOLE_MAX_INPUT];
	bool deferred_line_pending;

	int argc;
	char** argv;
	char* config_path;

	AssetPaths* paths;
	Window* win;

	// Pointer to the current config pointer in main (updated on reload).
	const CoreConfig** cfg;

	// Runtime feature toggles.
	bool* audio_enabled;
	bool* music_enabled;
	bool* sound_emitters_enabled;
	bool* light_emitters_enabled;
	bool* show_fps;
	bool* show_debug;
	bool* show_font_test;

	// Level/timeline state.
	MapLoadResult* map;
	bool* map_ok;
	char* map_name_buf;
	size_t map_name_cap;
	bool* using_timeline;
	Timeline* timeline;
	TimelineFlow* tl_flow;
	LevelMesh* mesh;

	// World state.
	Player* player;
	GameState* gs;
	EntitySystem* entities;
	EntityDefs* entity_defs;
	SoundEmitters* sfx_emitters;
	ParticleEmitters* particle_emitters;
	PerfTrace* perf;
	Framebuffer* fb;
	float* wall_depth;

	// Music bookkeeping.
	char* prev_bgmusic;
	size_t prev_bgmusic_cap;
	char* prev_soundfont;
	size_t prev_soundfont_cap;

	// Standalone screen runtime (used by developer-only screens such as Scenes).
	ScreenRuntime* screens;
} ConsoleCommandContext;

// Registers the built-in Mortum console commands onto `con`.
// `ctx` is not captured; it is passed via console_update()'s user_ctx.
void console_commands_register_all(Console* con);
