#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"
#include "game/world.h"
#include "game/entity.h"

typedef struct MapLoadResult {
	World world;
	EntityList entities;
	float player_start_x;
	float player_start_y;
	float player_start_angle_deg;
	char bgmusic[64];      // MIDI filename for background music
	char soundfont[64];    // SoundFont filename for background music
} MapLoadResult;

void map_load_result_destroy(MapLoadResult* self);

bool map_load(MapLoadResult* out, const AssetPaths* paths, const char* map_filename);
