#include "assets/map_validate.h"

#include "core/log.h"

bool map_validate(const World* world) {
	if (!world) {
		return false;
	}
	if (world->sector_count <= 0) {
		log_error("Map must have at least one sector");
		return false;
	}
	for (int i = 0; i < world->sector_count; i++) {
		if (world->sectors[i].ceil_z <= world->sectors[i].floor_z) {
			log_error("Sector %d has ceil_z <= floor_z", i);
			return false;
		}
		if (world->sectors[i].floor_tex[0] == '\0') {
			log_error("Sector %d missing floor_tex", i);
			return false;
		}
		if (world->sectors[i].ceil_tex[0] == '\0') {
			log_error("Sector %d missing ceil_tex", i);
			return false;
		}
	}
	for (int i = 0; i < world->wall_count; i++) {
		Wall w = world->walls[i];
		if (w.v0 < 0 || w.v0 >= world->vertex_count || w.v1 < 0 || w.v1 >= world->vertex_count) {
			log_error("Wall %d vertex indices out of range", i);
			return false;
		}
		if (w.tex[0] == '\0') {
			log_error("Wall %d missing tex", i);
			return false;
		}
	}
	return true;
}
