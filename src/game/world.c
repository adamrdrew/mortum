#include "game/world.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

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

bool world_sector_contains_point(const World* world, int sector, float px, float py) {
	if (!world || (unsigned)sector >= (unsigned)world->sector_count) {
		return false;
	}

	// Prefer using only edges where this sector is the wall's front side.
	// Many portal boundaries are represented by two directed walls (A->B and B->A);
	// counting both would double-count the segment and break even-odd classification.
	int crossings = 0;
	int edge_count = 0;
	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (w->front_sector != sector) {
			continue;
		}
		edge_count++;
		if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		// Skip horizontal edges.
		if (fabsf(a.y - b.y) < 1e-8f) {
			continue;
		}
		bool cond = (a.y > py) != (b.y > py);
		if (!cond) {
			continue;
		}
		float x_int = (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x;
		if (px < x_int) {
			crossings ^= 1;
		}
	}
	if (edge_count > 0) {
		return crossings != 0;
	}

	// Fallback: if a sector has no front edges (older maps or malformed data),
	// include any wall that references the sector.
	crossings = 0;
	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (w->front_sector != sector && w->back_sector != sector) {
			continue;
		}
		if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		if (fabsf(a.y - b.y) < 1e-8f) {
			continue;
		}
		bool cond = (a.y > py) != (b.y > py);
		if (!cond) {
			continue;
		}
		float x_int = (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x;
		if (px < x_int) {
			crossings ^= 1;
		}
	}
	return crossings != 0;
}

int world_find_sector_at_point(const World* world, float x, float y) {
	if (!world || world->sector_count <= 0) {
		return -1;
	}
	for (int s = 0; s < world->sector_count; s++) {
		if (world_sector_contains_point(world, s, x, y)) {
			return s;
		}
	}
	return -1;
}

int world_find_sector_at_point_stable(const World* world, float x, float y, int last_valid_sector) {
	if (!world || world->sector_count <= 0) {
		return -1;
	}
	if ((unsigned)last_valid_sector >= (unsigned)world->sector_count) {
		last_valid_sector = -1;
	}
	int s = world_find_sector_at_point(world, x, y);
	if ((unsigned)s < (unsigned)world->sector_count) {
		return s;
	}
	return last_valid_sector;
}
