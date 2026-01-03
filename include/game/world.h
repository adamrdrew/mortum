#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/base.h"
#include "game/particles.h"
#include "render/lighting.h"

typedef struct Vertex {
	float x;
	float y;
} Vertex;

typedef struct Sector {
	int id;
	// Runtime floor height (can change for movable sectors).
	float floor_z;
	// Floor height as authored in the map file.
	float floor_z_origin;
	// Optional alternate floor height for toggleable/movable sectors.
	float floor_z_toggled_pos;
	bool movable;
	bool floor_moving;
	float floor_z_target;
	int floor_toggle_wall_index; // wall index that initiated current movement, or -1
	float ceil_z;
	float light;
	LightColor light_color;
	char floor_tex[64];
	char ceil_tex[64];
} Sector;

typedef struct Wall {
	int v0;
	int v1;
	int front_sector;
	int back_sector; // -1 for solid
	// If true, pressing the action key while touching this wall completes the level.
	bool end_level;
	// Runtime door state: when true, this wall behaves as solid even if back_sector is a portal.
	bool door_blocked;
	// Door open animation fraction in [0,1].
	// 0 = fully closed (blocks portal), 1 = fully open (raised through ceiling).
	// Meaningful only when door_blocked is true.
	float door_open_t;
	// Current wall texture (may change at runtime).
	char tex[64];
	// Inactive/base texture from the map file.
	char base_tex[64];
	// Optional active texture for toggle walls.
	char active_tex[64];
	// Optional inventory gating for toggle walls.
	char required_item[64];
	// Optional message when required_item is missing.
	char required_item_missing_message[128];
	// Optional toggle sounds (WAV under Assets/Sounds/Effects/).
	char toggle_sound[64];
	char toggle_sound_finish[64];
	bool toggle_sector;
	int toggle_sector_id; // -1 means "use default" (sector on player side)
	bool toggle_sector_oneshot;
} Wall;

typedef struct World {
	Vertex* vertices; // owned
	int vertex_count;
	Sector* sectors;  // owned
	int sector_count;
	Wall* walls;      // owned
	int wall_count;
	// Per-wall interaction debounce state (owned). Indexed by wall index.
	// Used for deterministic interaction cooldowns (e.g., toggle walls).
	float* wall_interact_next_allowed_s; // length wall_count
	float* wall_interact_next_deny_toast_s; // length wall_count
	// Optional acceleration structure: for each sector, a packed list of wall indices
	// that reference that sector (front and/or back). Built by world_build_sector_wall_index.
	int* sector_wall_offsets;      // owned, length sector_count+1
	int* sector_wall_counts;       // owned, length sector_count
	int* sector_wall_indices;      // owned, length sector_wall_index_count
	int sector_wall_index_count;
	PointLight* lights; // owned
	uint8_t* light_alive; // owned, size=light_capacity (0=free slot)
	int* light_free; // owned stack of free indices
	int light_free_count;
	int light_free_cap;
	int light_count; // total slots in use in lights[] (may include free slots)
	int light_capacity;

	// World-owned particle pool. Particles always run their lifecycle to completion
	// even if their originating emitter is destroyed.
	Particles particles;
} World;

void world_init_empty(World* self);
void world_destroy(World* self);

bool world_alloc_vertices(World* self, int count);
bool world_alloc_sectors(World* self, int count);
bool world_alloc_walls(World* self, int count);
bool world_alloc_lights(World* self, int count);

// Programmatic light emitters (runtime).
// Returns light index on success, or -1 on failure.
int world_light_spawn(World* self, PointLight light);
bool world_light_remove(World* self, int light_index);
bool world_light_set_pos(World* self, int light_index, float x, float y, float z);
bool world_light_set_intensity(World* self, int light_index, float intensity);
bool world_light_set_radius(World* self, int light_index, float radius);

// Build a per-sector wall index (acceleration structure).
// Safe to call multiple times; frees/rebuilds any existing index.
bool world_build_sector_wall_index(World* self);

void world_set_sector_tex(Sector* s, StringView floor_tex, StringView ceil_tex);
void world_set_wall_tex(Wall* w, StringView tex);

// Point-in-sector queries.
// Uses an even-odd test on wall edges belonging to the sector.
bool world_sector_contains_point(const World* world, int sector, float x, float y);

// Returns a sector *index* in [0, world->sector_count), or -1 if not inside any sector.
int world_find_sector_at_point(const World* world, float x, float y);

// Like world_find_sector_at_point, but falls back to last_valid_sector when the point is not
// inside any sector. Pass last_valid_sector as the last known-good sector index, or -1.
int world_find_sector_at_point_stable(const World* world, float x, float y, int last_valid_sector);
