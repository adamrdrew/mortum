#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "assets/asset_paths.h"
#include "assets/map_loader.h"
#include "assets/timeline_loader.h"

#include "game/entities.h"
#include "game/game_state.h"
#include "game/player.h"
#include "game/screen_runtime.h"
#include "game/sound_emitters.h"
#include "game/particle_emitters.h"

#include "platform/input.h"

#include "render/framebuffer.h"
#include "render/level_mesh.h"

// Timeline-driven flow: runs one event at a time (scene, map, or menu) and advances/loops/loads on completion.

typedef struct ConsoleCommandContext ConsoleCommandContext;
typedef struct Console Console;

typedef struct TimelineFlow {
	bool active;
	int index;
	// When a Scene is running under TimelineFlow, indicates whether that Scene's exit should preserve
	// the currently playing MIDI (used to chain into a music.no_stop scene).
	bool preserve_midi_on_scene_exit;
} TimelineFlow;

bool timeline_flow_preserve_midi_on_scene_exit(const TimelineFlow* self);

typedef struct TimelineFlowRuntime {
	const AssetPaths* paths;
	Console* con;

	Timeline* timeline;
	bool* using_timeline;

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
	ConsoleCommandContext* console_ctx;

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
} TimelineFlowRuntime;

void timeline_flow_init(TimelineFlow* self);

// Begins execution immediately (starts first event).
// Returns true if the flow is active after start (it may become inactive for empty timelines).
bool timeline_flow_start(TimelineFlow* self, TimelineFlowRuntime* rt);

bool timeline_flow_is_active(const TimelineFlow* self);

// Notify flow that the currently running screen (scene or menu) completed.
void timeline_flow_on_screen_completed(TimelineFlow* self, TimelineFlowRuntime* rt);

// Back-compat alias.
void timeline_flow_on_scene_completed(TimelineFlow* self, TimelineFlowRuntime* rt);

// Notify flow that gameplay entered WIN mode this frame.
void timeline_flow_on_map_win(TimelineFlow* self, TimelineFlowRuntime* rt);

// Cancels timeline progression (does not unload map). Marks flow inactive.
void timeline_flow_abort(TimelineFlow* self);
