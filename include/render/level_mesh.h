#pragma once

#include <stdbool.h>

#include "game/world.h"

typedef struct WallSegment {
	float ax;
	float ay;
	float bx;
	float by;
	int wall_index;
} WallSegment;

typedef struct LevelMesh {
	WallSegment* walls; // owned
	int wall_count;
} LevelMesh;

void level_mesh_init(LevelMesh* self);
void level_mesh_destroy(LevelMesh* self);

bool level_mesh_build(LevelMesh* self, const World* world);
