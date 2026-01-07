// Nomos Studio - Document model
// Represents the currently loaded/edited map with selection state and undo capability.
#pragma once

#include "nomos.h"
#include "assets/asset_paths.h"
#include "assets/map_loader.h"
#include "assets/map_validate.h"
#include "game/world.h"

#include <stdbool.h>
#include <limits.h>

// Maximum path length
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// Selection state
typedef struct NomosSelection {
	NomosSelectionType type;
	int index;  // Index into the relevant array (sectors, walls, entities, etc.)
} NomosSelection;

// Document state
typedef struct NomosDocument {
	// The loaded map data (owned)
	bool has_map;
	MapLoadResult map;
	
	// File path (relative to Levels/, e.g., "my_map.json")
	char file_path[PATH_MAX];
	
	// Dirty flag
	bool dirty;
	
	// Selection
	NomosSelection selection;
	
	// Hover state (for UI feedback)
	NomosSelection hover;
	
	// Validation results (owned)
	bool has_validation;
	MapValidationReport validation;
	
	// Scroll positions for validation results panel
	int validation_scroll;
	
} NomosDocument;

// Document lifecycle
void nomos_document_init(NomosDocument* doc);
void nomos_document_destroy(NomosDocument* doc);
void nomos_document_clear(NomosDocument* doc);

// Document operations
bool nomos_document_load(NomosDocument* doc, const AssetPaths* paths, const char* map_filename);
bool nomos_document_save(NomosDocument* doc, const AssetPaths* paths);
bool nomos_document_validate(NomosDocument* doc, const AssetPaths* paths);
bool nomos_document_run_in_mortum(NomosDocument* doc, const AssetPaths* paths);

// Selection operations
void nomos_document_select(NomosDocument* doc, NomosSelectionType type, int index);
void nomos_document_deselect_all(NomosDocument* doc);
void nomos_document_delete_selected(NomosDocument* doc);

// Entity placement operations
int nomos_document_add_entity(NomosDocument* doc, const char* def_name, float x, float y);
bool nomos_document_move_entity(NomosDocument* doc, int index, float x, float y);
bool nomos_document_remove_entity(NomosDocument* doc, int index);

// Light placement operations
int nomos_document_add_light(NomosDocument* doc, float x, float y, float z, float radius, float intensity);
bool nomos_document_move_light(NomosDocument* doc, int index, float x, float y);
bool nomos_document_remove_light(NomosDocument* doc, int index);
bool nomos_document_set_light_property(NomosDocument* doc, int index, const char* property, float value);

// Particle emitter placement operations
int nomos_document_add_particle(NomosDocument* doc, float x, float y, float z);
bool nomos_document_move_particle(NomosDocument* doc, int index, float x, float y);
bool nomos_document_remove_particle(NomosDocument* doc, int index);

// Player start operations
bool nomos_document_move_player_start(NomosDocument* doc, float x, float y);
bool nomos_document_set_player_start_angle(NomosDocument* doc, float angle_deg);

// Sector property editing
bool nomos_document_set_sector_floor_z(NomosDocument* doc, int index, float value);
bool nomos_document_set_sector_ceil_z(NomosDocument* doc, int index, float value);
bool nomos_document_set_sector_floor_tex(NomosDocument* doc, int index, const char* tex);
bool nomos_document_set_sector_ceil_tex(NomosDocument* doc, int index, const char* tex);
bool nomos_document_set_sector_light(NomosDocument* doc, int index, float value);

// Wall property editing
bool nomos_document_set_wall_tex(NomosDocument* doc, int index, const char* tex);
bool nomos_document_set_wall_end_level(NomosDocument* doc, int index, bool value);

// Query helpers
bool nomos_document_get_world_bounds(const NomosDocument* doc, float* min_x, float* min_y, float* max_x, float* max_y);
int nomos_document_find_sector_at_point(const NomosDocument* doc, float x, float y);
