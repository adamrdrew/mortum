#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "assets/asset_paths.h"
#include "assets/episode_loader.h"
#include "assets/map_loader.h"

#include "core/config.h"

#include "game/console.h"
#include "game/entities.h"
#include "game/episode_runner.h"
#include "game/game_state.h"
#include "game/perf_trace.h"
#include "game/player.h"
#include "game/sound_emitters.h"

#include "platform/window.h"

#include "render/framebuffer.h"
#include "render/level_mesh.h"

// Command wiring context. Commands call into engine systems through pointers here.
// Keep this POD and owned by main.

typedef struct ConsoleCommandContext {
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

	// Level/episode state.
	MapLoadResult* map;
	bool* map_ok;
	char* map_name_buf;
	size_t map_name_cap;
	bool* using_episode;
	Episode* ep;
	EpisodeRunner* runner;
	LevelMesh* mesh;

	// World state.
	Player* player;
	GameState* gs;
	EntitySystem* entities;
	EntityDefs* entity_defs;
	SoundEmitters* sfx_emitters;
	PerfTrace* perf;
	Framebuffer* fb;
	float* wall_depth;

	// Music bookkeeping.
	char* prev_bgmusic;
	size_t prev_bgmusic_cap;
	char* prev_soundfont;
	size_t prev_soundfont_cap;
} ConsoleCommandContext;

// Registers the built-in Mortum console commands onto `con`.
// `ctx` is not captured; it is passed via console_update()'s user_ctx.
void console_commands_register_all(Console* con);
