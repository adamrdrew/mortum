#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "assets/asset_paths.h"
#include "assets/episode_loader.h"
#include "assets/map_loader.h"

#include "game/entities.h"
#include "game/episode_runner.h"
#include "game/game_state.h"
#include "game/player.h"
#include "game/screen_runtime.h"
#include "game/sound_emitters.h"
#include "game/particle_emitters.h"

#include "platform/input.h"

#include "render/framebuffer.h"
#include "render/level_mesh.h"

// Episode-driven flow: enter scenes -> maps -> exit scenes.

typedef enum EpisodeFlowPhase {
	EPISODE_FLOW_PHASE_ENTER_SCENES = 0,
	EPISODE_FLOW_PHASE_MAPS = 1,
	EPISODE_FLOW_PHASE_EXIT_SCENES = 2,
	EPISODE_FLOW_PHASE_DONE = 3,
} EpisodeFlowPhase;

typedef struct EpisodeFlow {
	bool active;
	EpisodeFlowPhase phase;
	int enter_index;
	int exit_index;
	// When a scene is running under EpisodeFlow, this indicates whether that scene's exit
	// should preserve currently playing MIDI (used to implement chaining into a music.no_stop scene).
	bool preserve_midi_on_scene_exit;
} EpisodeFlow;

// If EpisodeFlow is currently running a Scene, returns whether that Scene should preserve
// currently playing MIDI when it exits.
bool episode_flow_preserve_midi_on_scene_exit(const EpisodeFlow* self);

typedef struct EpisodeFlowRuntime {
	const AssetPaths* paths;

	Episode* ep;
	EpisodeRunner* runner;
	bool* using_episode;

	MapLoadResult* map;
	bool* map_ok;
	char* map_name_buf;
	size_t map_name_cap;

	LevelMesh* mesh;
	Player* player;
	GameState* gs;
	EntitySystem* entities;
	EntityDefs* entity_defs;
	SoundEmitters* sfx_emitters;
	ParticleEmitters* particle_emitters;

	// Screen system for Scenes.
	ScreenRuntime* screens;
	Framebuffer* fb;

	// For ScreenContext (update-time input comes from main; for on_enter it may be NULL).
	const Input* in;
	bool allow_scene_input;
	bool audio_enabled;
	bool music_enabled;
	bool sound_emitters_enabled;

	// Music restore bookkeeping used by main (optional).
	char* prev_bgmusic;
	size_t prev_bgmusic_cap;
	char* prev_soundfont;
	size_t prev_soundfont_cap;
} EpisodeFlowRuntime;

void episode_flow_init(EpisodeFlow* self);

// Resets flow state and begins execution immediately (starts enter scene or loads first map).
// Returns true if flow was started (even if it immediately reaches DONE).
bool episode_flow_start(EpisodeFlow* self, EpisodeFlowRuntime* rt);

// Notify flow that the currently running scene screen just completed.
void episode_flow_on_scene_completed(EpisodeFlow* self, EpisodeFlowRuntime* rt);

// Notify flow that gameplay entered WIN mode this frame.
void episode_flow_on_map_win(EpisodeFlow* self, EpisodeFlowRuntime* rt);

// Cancels episode progression (does not unload map). Marks flow inactive.
void episode_flow_cancel(EpisodeFlow* self);
