// Nomos Studio - Document implementation
#include "nomos_document.h"
#include "nomos_save.h"

#include "assets/map_loader.h"
#include "assets/map_validate.h"
#include "core/log.h"
#include "game/world.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

void nomos_document_init(NomosDocument* doc) {
	if (!doc) return;
	memset(doc, 0, sizeof(*doc));
	doc->selection.type = NOMOS_SEL_NONE;
	doc->selection.index = -1;
	doc->hover.type = NOMOS_SEL_NONE;
	doc->hover.index = -1;
}

void nomos_document_destroy(NomosDocument* doc) {
	if (!doc) return;
	nomos_document_clear(doc);
}

void nomos_document_clear(NomosDocument* doc) {
	if (!doc) return;
	
	if (doc->has_map) {
		map_load_result_destroy(&doc->map);
		doc->has_map = false;
	}
	
	if (doc->has_validation) {
		map_validation_report_destroy(&doc->validation);
		doc->has_validation = false;
	}
	
	doc->file_path[0] = '\0';
	doc->dirty = false;
	doc->selection.type = NOMOS_SEL_NONE;
	doc->selection.index = -1;
	doc->hover.type = NOMOS_SEL_NONE;
	doc->hover.index = -1;
	doc->validation_scroll = 0;
}

bool nomos_document_load(NomosDocument* doc, const AssetPaths* paths, const char* map_filename) {
	if (!doc || !paths || !map_filename || map_filename[0] == '\0') {
		return false;
	}
	
	// Try to load the new map
	MapLoadResult new_map;
	if (!map_load(&new_map, paths, map_filename)) {
		fprintf(stderr, "Failed to load map: %s\n", map_filename);
		return false;
	}
	
	// Success - clear old document and replace
	nomos_document_clear(doc);
	doc->map = new_map;
	doc->has_map = true;
	strncpy(doc->file_path, map_filename, sizeof(doc->file_path) - 1);
	doc->file_path[sizeof(doc->file_path) - 1] = '\0';
	doc->dirty = false;
	
	return true;
}

bool nomos_document_save(NomosDocument* doc, const AssetPaths* paths) {
	if (!doc || !paths || !doc->has_map || doc->file_path[0] == '\0') {
		return false;
	}
	
	// Build the full path
	char* full_path = asset_path_join(paths, "Levels", doc->file_path);
	if (!full_path) {
		return false;
	}
	
	// Save using our save module
	bool ok = nomos_save_map(&doc->map, full_path);
	free(full_path);
	
	if (ok) {
		doc->dirty = false;
	}
	
	return ok;
}

bool nomos_document_validate(NomosDocument* doc, const AssetPaths* paths) {
	(void)paths;
	
	if (!doc || !doc->has_map) {
		return false;
	}
	
	// Clear old validation results
	if (doc->has_validation) {
		map_validation_report_destroy(&doc->validation);
		doc->has_validation = false;
	}
	
	// Initialize new report
	map_validation_report_init(&doc->validation);
	
	// Set as the report sink
	map_validate_set_report_sink(&doc->validation);
	
	// Run validation
	bool valid = map_validate(
		&doc->map.world,
		doc->map.player_start_x,
		doc->map.player_start_y,
		doc->map.doors,
		doc->map.door_count
	);
	
	// Clear the sink
	map_validate_set_report_sink(NULL);
	
	doc->has_validation = true;
	doc->validation_scroll = 0;
	
	printf("Validation complete: %s (%d errors, %d warnings)\n",
		valid ? "VALID" : "INVALID",
		doc->validation.error_count,
		doc->validation.warning_count);
	
	return valid;
}

bool nomos_document_run_in_mortum(NomosDocument* doc, const AssetPaths* paths) {
	if (!doc || !paths || !doc->has_map || doc->file_path[0] == '\0') {
		fprintf(stderr, "Cannot run: no map loaded\n");
		return false;
	}
	
	// If dirty, save first
	if (doc->dirty) {
		if (!nomos_document_save(doc, paths)) {
			fprintf(stderr, "Failed to save before running\n");
			return false;
		}
	}
	
	// Find the mortum executable
	// Try several locations relative to the assets root
	char mortum_path[512];
	const char* candidates[] = {
		"build/mortum",
		"./build/mortum",
		"../build/mortum",
		"mortum",
		"./mortum"
	};
	
	const char* found_path = NULL;
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		if (access(candidates[i], X_OK) == 0) {
			found_path = candidates[i];
			break;
		}
	}
	
	if (!found_path) {
		// Try relative to assets root
		snprintf(mortum_path, sizeof(mortum_path), "%s/../build/mortum", paths->assets_root);
		if (access(mortum_path, X_OK) == 0) {
			found_path = mortum_path;
		}
	}
	
	if (!found_path) {
		fprintf(stderr, "Could not find mortum executable\n");
		return false;
	}
	
	printf("Launching mortum: %s with MAP=%s\n", found_path, doc->file_path);
	
	// Set MAP environment variable and spawn
	setenv("MAP", doc->file_path, 1);
	
	pid_t pid;
	char* argv[] = { (char*)found_path, NULL };
	
	int status = posix_spawn(&pid, found_path, NULL, NULL, argv, environ);
	if (status != 0) {
		fprintf(stderr, "Failed to spawn mortum: %d\n", status);
		return false;
	}
	
	printf("Launched mortum (PID: %d)\n", pid);
	return true;
}

// Selection operations
void nomos_document_select(NomosDocument* doc, NomosSelectionType type, int index) {
	if (!doc) return;
	doc->selection.type = type;
	doc->selection.index = index;
}

void nomos_document_deselect_all(NomosDocument* doc) {
	if (!doc) return;
	doc->selection.type = NOMOS_SEL_NONE;
	doc->selection.index = -1;
}

void nomos_document_delete_selected(NomosDocument* doc) {
	if (!doc || !doc->has_map) return;
	
	switch (doc->selection.type) {
		case NOMOS_SEL_ENTITY:
			nomos_document_remove_entity(doc, doc->selection.index);
			break;
		case NOMOS_SEL_LIGHT:
			nomos_document_remove_light(doc, doc->selection.index);
			break;
		case NOMOS_SEL_PARTICLE:
			nomos_document_remove_particle(doc, doc->selection.index);
			break;
		default:
			// Sectors, walls, player start cannot be deleted
			break;
	}
	
	nomos_document_deselect_all(doc);
}

// Entity operations
int nomos_document_add_entity(NomosDocument* doc, const char* def_name, float x, float y) {
	if (!doc || !doc->has_map || !def_name) return -1;
	
	// Find sector at position
	int sector = nomos_document_find_sector_at_point(doc, x, y);
	if (sector < 0) {
		fprintf(stderr, "Cannot place entity: position not inside any sector\n");
		return -1;
	}
	
	// Grow entity array
	int new_count = doc->map.entity_count + 1;
	MapEntityPlacement* new_entities = realloc(doc->map.entities, (size_t)new_count * sizeof(MapEntityPlacement));
	if (!new_entities) return -1;
	
	doc->map.entities = new_entities;
	int index = doc->map.entity_count;
	doc->map.entity_count = new_count;
	
	// Initialize new entity
	MapEntityPlacement* e = &doc->map.entities[index];
	memset(e, 0, sizeof(*e));
	e->x = x;
	e->y = y;
	e->yaw_deg = 0.0f;
	e->sector = sector;
	strncpy(e->def_name, def_name, sizeof(e->def_name) - 1);
	
	doc->dirty = true;
	return index;
}

bool nomos_document_move_entity(NomosDocument* doc, int index, float x, float y) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.entity_count) return false;
	
	// Check if new position is in a valid sector
	int sector = nomos_document_find_sector_at_point(doc, x, y);
	if (sector < 0) return false;
	
	doc->map.entities[index].x = x;
	doc->map.entities[index].y = y;
	doc->map.entities[index].sector = sector;
	doc->dirty = true;
	return true;
}

bool nomos_document_remove_entity(NomosDocument* doc, int index) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.entity_count) return false;
	
	// Shift remaining entities
	for (int i = index; i < doc->map.entity_count - 1; i++) {
		doc->map.entities[i] = doc->map.entities[i + 1];
	}
	doc->map.entity_count--;
	doc->dirty = true;
	return true;
}

// Light operations
int nomos_document_add_light(NomosDocument* doc, float x, float y, float z, float radius, float intensity) {
	if (!doc || !doc->has_map) return -1;
	
	// Check if position is in a valid sector
	int sector = nomos_document_find_sector_at_point(doc, x, y);
	if (sector < 0) {
		fprintf(stderr, "Cannot place light: position not inside any sector\n");
		return -1;
	}
	
	// Use world_light_spawn
	PointLight light;
	memset(&light, 0, sizeof(light));
	light.x = x;
	light.y = y;
	light.z = z;
	light.radius = radius;
	light.intensity = intensity;
	light.color.r = 1.0f;
	light.color.g = 1.0f;
	light.color.b = 1.0f;
	light.flicker = LIGHT_FLICKER_NONE;
	light.seed = 0;
	
	int index = world_light_spawn(&doc->map.world, light);
	if (index >= 0) {
		doc->dirty = true;
	}
	return index;
}

bool nomos_document_move_light(NomosDocument* doc, int index, float x, float y) {
	if (!doc || !doc->has_map) return false;
	
	// Check if new position is in a valid sector
	int sector = nomos_document_find_sector_at_point(doc, x, y);
	if (sector < 0) return false;
	
	// Get current z and update position
	if (index < 0 || index >= doc->map.world.light_count) return false;
	if (!doc->map.world.light_alive || !doc->map.world.light_alive[index]) return false;
	
	float z = doc->map.world.lights[index].z;
	if (world_light_set_pos(&doc->map.world, index, x, y, z)) {
		doc->dirty = true;
		return true;
	}
	return false;
}

bool nomos_document_remove_light(NomosDocument* doc, int index) {
	if (!doc || !doc->has_map) return false;
	
	if (world_light_remove(&doc->map.world, index)) {
		doc->dirty = true;
		return true;
	}
	return false;
}

bool nomos_document_set_light_property(NomosDocument* doc, int index, const char* property, float value) {
	if (!doc || !doc->has_map || !property) return false;
	if (index < 0 || index >= doc->map.world.light_count) return false;
	if (!doc->map.world.light_alive || !doc->map.world.light_alive[index]) return false;
	
	PointLight* light = &doc->map.world.lights[index];
	
	if (strcmp(property, "radius") == 0) {
		light->radius = value > 0 ? value : 0;
		doc->dirty = true;
		return true;
	}
	if (strcmp(property, "intensity") == 0) {
		light->intensity = value > 0 ? value : 0;
		doc->dirty = true;
		return true;
	}
	if (strcmp(property, "z") == 0) {
		light->z = value;
		doc->dirty = true;
		return true;
	}
	if (strcmp(property, "color_r") == 0) {
		light->color.r = value;
		doc->dirty = true;
		return true;
	}
	if (strcmp(property, "color_g") == 0) {
		light->color.g = value;
		doc->dirty = true;
		return true;
	}
	if (strcmp(property, "color_b") == 0) {
		light->color.b = value;
		doc->dirty = true;
		return true;
	}
	
	return false;
}

// Particle operations
int nomos_document_add_particle(NomosDocument* doc, float x, float y, float z) {
	if (!doc || !doc->has_map) return -1;
	
	// Check if position is in a valid sector
	int sector = nomos_document_find_sector_at_point(doc, x, y);
	if (sector < 0) {
		fprintf(stderr, "Cannot place particle emitter: position not inside any sector\n");
		return -1;
	}
	
	// Grow particle array
	int new_count = doc->map.particle_count + 1;
	MapParticleEmitter* new_particles = realloc(doc->map.particles, (size_t)new_count * sizeof(MapParticleEmitter));
	if (!new_particles) return -1;
	
	doc->map.particles = new_particles;
	int index = doc->map.particle_count;
	doc->map.particle_count = new_count;
	
	// Initialize with defaults
	MapParticleEmitter* p = &doc->map.particles[index];
	memset(p, 0, sizeof(*p));
	p->x = x;
	p->y = y;
	p->z = z;
	// Set some reasonable defaults
	p->def.particle_life_ms = 1000;
	p->def.emit_interval_ms = 100;
	p->def.offset_jitter = 0.1f;
	p->def.shape = PARTICLE_SHAPE_CIRCLE;
	p->def.start.opacity = 1.0f;
	p->def.start.size = 0.1f;
	p->def.start.color.r = 1.0f;
	p->def.start.color.g = 1.0f;
	p->def.start.color.b = 1.0f;
	p->def.start.color.opacity = 1.0f;
	p->def.end.opacity = 0.0f;
	p->def.end.size = 0.2f;
	p->def.end.color.r = 1.0f;
	p->def.end.color.g = 1.0f;
	p->def.end.color.b = 1.0f;
	p->def.end.color.opacity = 0.0f;
	
	doc->dirty = true;
	return index;
}

bool nomos_document_move_particle(NomosDocument* doc, int index, float x, float y) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.particle_count) return false;
	
	// Check if new position is in a valid sector
	int sector = nomos_document_find_sector_at_point(doc, x, y);
	if (sector < 0) return false;
	
	doc->map.particles[index].x = x;
	doc->map.particles[index].y = y;
	doc->dirty = true;
	return true;
}

bool nomos_document_remove_particle(NomosDocument* doc, int index) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.particle_count) return false;
	
	// Shift remaining particles
	for (int i = index; i < doc->map.particle_count - 1; i++) {
		doc->map.particles[i] = doc->map.particles[i + 1];
	}
	doc->map.particle_count--;
	doc->dirty = true;
	return true;
}

// Player start operations
bool nomos_document_move_player_start(NomosDocument* doc, float x, float y) {
	if (!doc || !doc->has_map) return false;
	
	// Check if new position is in a valid sector
	int sector = nomos_document_find_sector_at_point(doc, x, y);
	if (sector < 0) return false;
	
	doc->map.player_start_x = x;
	doc->map.player_start_y = y;
	doc->dirty = true;
	return true;
}

bool nomos_document_set_player_start_angle(NomosDocument* doc, float angle_deg) {
	if (!doc || !doc->has_map) return false;
	doc->map.player_start_angle_deg = angle_deg;
	doc->dirty = true;
	return true;
}

// Sector property editing
bool nomos_document_set_sector_floor_z(NomosDocument* doc, int index, float value) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.world.sector_count) return false;
	
	Sector* s = &doc->map.world.sectors[index];
	if (value >= s->ceil_z) return false; // floor must be below ceiling
	
	s->floor_z = value;
	s->floor_z_origin = value;
	doc->dirty = true;
	return true;
}

bool nomos_document_set_sector_ceil_z(NomosDocument* doc, int index, float value) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.world.sector_count) return false;
	
	Sector* s = &doc->map.world.sectors[index];
	if (value <= s->floor_z) return false; // ceiling must be above floor
	
	s->ceil_z = value;
	doc->dirty = true;
	return true;
}

bool nomos_document_set_sector_floor_tex(NomosDocument* doc, int index, const char* tex) {
	if (!doc || !doc->has_map || !tex) return false;
	if (index < 0 || index >= doc->map.world.sector_count) return false;
	
	strncpy(doc->map.world.sectors[index].floor_tex, tex, sizeof(doc->map.world.sectors[index].floor_tex) - 1);
	doc->dirty = true;
	return true;
}

bool nomos_document_set_sector_ceil_tex(NomosDocument* doc, int index, const char* tex) {
	if (!doc || !doc->has_map || !tex) return false;
	if (index < 0 || index >= doc->map.world.sector_count) return false;
	
	strncpy(doc->map.world.sectors[index].ceil_tex, tex, sizeof(doc->map.world.sectors[index].ceil_tex) - 1);
	doc->dirty = true;
	return true;
}

bool nomos_document_set_sector_light(NomosDocument* doc, int index, float value) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.world.sector_count) return false;
	
	doc->map.world.sectors[index].light = value;
	doc->dirty = true;
	return true;
}

// Wall property editing
bool nomos_document_set_wall_tex(NomosDocument* doc, int index, const char* tex) {
	if (!doc || !doc->has_map || !tex) return false;
	if (index < 0 || index >= doc->map.world.wall_count) return false;
	
	strncpy(doc->map.world.walls[index].tex, tex, sizeof(doc->map.world.walls[index].tex) - 1);
	strncpy(doc->map.world.walls[index].base_tex, tex, sizeof(doc->map.world.walls[index].base_tex) - 1);
	doc->dirty = true;
	return true;
}

bool nomos_document_set_wall_end_level(NomosDocument* doc, int index, bool value) {
	if (!doc || !doc->has_map) return false;
	if (index < 0 || index >= doc->map.world.wall_count) return false;
	
	doc->map.world.walls[index].end_level = value;
	doc->dirty = true;
	return true;
}

// Query helpers
bool nomos_document_get_world_bounds(const NomosDocument* doc, float* min_x, float* min_y, float* max_x, float* max_y) {
	if (!doc || !doc->has_map) return false;
	
	const World* w = &doc->map.world;
	if (w->vertex_count == 0) return false;
	
	float mnx = w->vertices[0].x;
	float mny = w->vertices[0].y;
	float mxx = w->vertices[0].x;
	float mxy = w->vertices[0].y;
	
	for (int i = 1; i < w->vertex_count; i++) {
		if (w->vertices[i].x < mnx) mnx = w->vertices[i].x;
		if (w->vertices[i].y < mny) mny = w->vertices[i].y;
		if (w->vertices[i].x > mxx) mxx = w->vertices[i].x;
		if (w->vertices[i].y > mxy) mxy = w->vertices[i].y;
	}
	
	if (min_x) *min_x = mnx;
	if (min_y) *min_y = mny;
	if (max_x) *max_x = mxx;
	if (max_y) *max_y = mxy;
	return true;
}

int nomos_document_find_sector_at_point(const NomosDocument* doc, float x, float y) {
	if (!doc || !doc->has_map) return -1;
	return world_find_sector_at_point(&doc->map.world, x, y);
}
