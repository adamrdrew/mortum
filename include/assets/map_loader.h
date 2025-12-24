#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"
#include "game/world.h"

// Map-authored entity placements.
// These are authored spawn points; the runtime entity system is responsible for creating
// live entities from them.
typedef struct MapEntityPlacement {
	float x;
	float y;
	float yaw_deg;
	int sector;
	char def_name[64];
} MapEntityPlacement;

typedef struct MapLoadResult {
	World world;
	float player_start_x;
	float player_start_y;
	float player_start_angle_deg;
	char bgmusic[64];      // MIDI filename for background music
	char soundfont[64];    // SoundFont filename for background music
	char sky[64];          // Optional skybox filename (loaded from Assets/Images/Sky)

	// Optional: map-authored sound emitters.
	// These are definitions; the runtime SoundEmitter system is responsible for creating
	// live emitters from them.
	struct MapSoundEmitter* sounds; // owned
	int sound_count;

	// Optional: map-authored entities.
	MapEntityPlacement* entities; // owned
	int entity_count;
} MapLoadResult;

typedef struct MapSoundEmitter {
	float x;
	float y;
	bool loop;
	bool spatial;
	float gain;
	char sound[64]; // WAV filename under Assets/Sounds/Effects/
} MapSoundEmitter;

void map_load_result_destroy(MapLoadResult* self);

bool map_load(MapLoadResult* out, const AssetPaths* paths, const char* map_filename);
