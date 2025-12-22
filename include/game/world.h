#pragma once

#include <stdbool.h>

#include "core/base.h"
#include "render/lighting.h"

typedef struct Vertex {
	float x;
	float y;
} Vertex;

typedef struct Sector {
	int id;
	float floor_z;
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
	char tex[64];
} Wall;

typedef struct World {
	Vertex* vertices; // owned
	int vertex_count;
	Sector* sectors;  // owned
	int sector_count;
	Wall* walls;      // owned
	int wall_count;
	PointLight* lights; // owned
	int light_count;
} World;

void world_init_empty(World* self);
void world_destroy(World* self);

bool world_alloc_vertices(World* self, int count);
bool world_alloc_sectors(World* self, int count);
bool world_alloc_walls(World* self, int count);
bool world_alloc_lights(World* self, int count);

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
