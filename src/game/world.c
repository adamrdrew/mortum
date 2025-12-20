#include "game/world.h"

#include <stdlib.h>
#include <string.h>

void world_init_empty(World* self) {
	memset(self, 0, sizeof(*self));
}

void world_destroy(World* self) {
	free(self->vertices);
	free(self->sectors);
	free(self->walls);
	free(self->lights);
	memset(self, 0, sizeof(*self));
}

bool world_alloc_vertices(World* self, int count) {
	self->vertices = (Vertex*)calloc((size_t)count, sizeof(Vertex));
	if (!self->vertices) {
		self->vertex_count = 0;
		return false;
	}
	self->vertex_count = count;
	return true;
}

bool world_alloc_sectors(World* self, int count) {
	self->sectors = (Sector*)calloc((size_t)count, sizeof(Sector));
	if (!self->sectors) {
		self->sector_count = 0;
		return false;
	}
	self->sector_count = count;
	for (int i = 0; i < count; i++) {
		self->sectors[i].light = 1.0f;
		self->sectors[i].light_color = light_color_white();
	}
	return true;
}

bool world_alloc_walls(World* self, int count) {
	self->walls = (Wall*)calloc((size_t)count, sizeof(Wall));
	if (!self->walls) {
		self->wall_count = 0;
		return false;
	}
	self->wall_count = count;
	return true;
}

bool world_alloc_lights(World* self, int count) {
	self->lights = (PointLight*)calloc((size_t)count, sizeof(PointLight));
	if (!self->lights) {
		self->light_count = 0;
		return false;
	}
	self->light_count = count;
	for (int i = 0; i < count; i++) {
		self->lights[i].color = light_color_white();
	}
	return true;
}

static void copy_sv(char dst[64], StringView s) {
	size_t n = s.len < 63 ? s.len : 63;
	memcpy(dst, s.data, n);
	dst[n] = '\0';
}

void world_set_sector_tex(Sector* s, StringView floor_tex, StringView ceil_tex) {
	copy_sv(s->floor_tex, floor_tex);
	copy_sv(s->ceil_tex, ceil_tex);
}

void world_set_wall_tex(Wall* w, StringView tex) {
	copy_sv(w->tex, tex);
}
