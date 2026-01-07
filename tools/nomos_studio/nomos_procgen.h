// Nomos Studio - Procedural map generation header
#ifndef NOMOS_PROCGEN_H
#define NOMOS_PROCGEN_H

#include "assets/map_loader.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct NomosProcGenParams {
	// Map bounds
	float min_x;
	float min_y;
	float max_x;
	float max_y;
	
	// Room generation
	int target_room_count;   // Approximate number of rooms to generate
	float min_room_size;     // Minimum room dimension
	float max_room_size;     // Maximum room dimension
	
	// Height variation
	float min_floor_z;
	float max_floor_z;
	float min_ceil_height;   // Minimum ceiling height above floor
	float max_ceil_height;   // Maximum ceiling height above floor
	
	// Random seed (0 = use time-based seed)
	uint32_t seed;
	
	// Textures
	char floor_tex[64];
	char ceil_tex[64];
	char wall_tex[64];
} NomosProcGenParams;

// Initialize params with reasonable defaults
void nomos_procgen_params_default(NomosProcGenParams* params);

// Generate a map using BSP partitioning
// Returns true on success, fills out the world with generated geometry
// After generation, validate using map_validate()
bool nomos_procgen_generate(MapLoadResult* out, const NomosProcGenParams* params);

#endif // NOMOS_PROCGEN_H
