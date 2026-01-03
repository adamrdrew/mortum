#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"
#include "game/world.h"

#include "game/particle_emitters.h"

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

// Map-authored door definitions (first-class primitive).
// Doors bind to an existing portal wall by index but have their own IDs, gating, and visuals.
typedef struct MapDoor {
	char id[64];
	int wall_index; // index into world.walls[]; must refer to a portal wall (back_sector != -1)
	bool starts_closed; // default true
	char tex[64]; // door slab texture when closed
	char sound_open[64]; // optional WAV filename under Assets/Sounds/Effects/
	char required_item[64]; // optional inventory item required to open
	char required_item_missing_message[128]; // optional toast message when missing
} MapDoor;

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

	// Optional: map-authored particle emitters.
	// These are definitions; the runtime ParticleEmitter system is responsible for creating
	// live emitters from them. Particles themselves are world-owned.
	struct MapParticleEmitter* particles; // owned
	int particle_count;

	// Optional: map-authored entities.
	MapEntityPlacement* entities; // owned
	int entity_count;

	// Optional: map-authored doors.
	MapDoor* doors; // owned
	int door_count;
} MapLoadResult;

typedef struct MapSoundEmitter {
	float x;
	float y;
	bool loop;
	bool spatial;
	float gain;
	char sound[64]; // WAV filename under Assets/Sounds/Effects/
} MapSoundEmitter;

typedef struct MapParticleEmitter {
	float x;
	float y;
	float z;
	ParticleEmitterDef def;
} MapParticleEmitter;

void map_load_result_destroy(MapLoadResult* self);

bool map_load(MapLoadResult* out, const AssetPaths* paths, const char* map_filename);
