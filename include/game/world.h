#pragma once

#include <stdbool.h>

#include "core/base.h"

typedef struct Vertex {
	float x;
	float y;
} Vertex;

typedef struct Sector {
	int id;
	float floor_z;
	float ceil_z;
	float light;
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
} World;

void world_init_empty(World* self);
void world_destroy(World* self);

bool world_alloc_vertices(World* self, int count);
bool world_alloc_sectors(World* self, int count);
bool world_alloc_walls(World* self, int count);

void world_set_sector_tex(Sector* s, StringView floor_tex, StringView ceil_tex);
void world_set_wall_tex(Wall* w, StringView tex);
