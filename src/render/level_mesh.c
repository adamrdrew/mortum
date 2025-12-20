#include "render/level_mesh.h"

#include <stdlib.h>
#include <string.h>

void level_mesh_init(LevelMesh* self) {
	memset(self, 0, sizeof(*self));
}

void level_mesh_destroy(LevelMesh* self) {
	free(self->walls);
	memset(self, 0, sizeof(*self));
}

bool level_mesh_build(LevelMesh* self, const World* world) {
	level_mesh_destroy(self);
	level_mesh_init(self);
	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		return false;
	}
	self->walls = (WallSegment*)calloc((size_t)world->wall_count, sizeof(WallSegment));
	if (!self->walls) {
		return false;
	}
	self->wall_count = world->wall_count;
	for (int i = 0; i < world->wall_count; i++) {
		Wall w = world->walls[i];
		if (w.v0 < 0 || w.v0 >= world->vertex_count || w.v1 < 0 || w.v1 >= world->vertex_count) {
			self->walls[i].ax = 0;
			self->walls[i].ay = 0;
			self->walls[i].bx = 0;
			self->walls[i].by = 0;
			self->walls[i].wall_index = i;
			continue;
		}
		Vertex a = world->vertices[w.v0];
		Vertex b = world->vertices[w.v1];
		self->walls[i].ax = a.x;
		self->walls[i].ay = a.y;
		self->walls[i].bx = b.x;
		self->walls[i].by = b.y;
		self->walls[i].wall_index = i;
	}
	return true;
}
