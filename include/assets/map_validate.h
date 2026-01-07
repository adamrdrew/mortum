#pragma once

#include <stdbool.h>

#include <math.h>

#include "game/world.h"

struct MapDoor;

typedef struct MapValidationContext {
	int sector_index; // -1 if unset
	int wall_index;   // -1 if unset
	int vertex_index; // -1 if unset
	int door_index;   // -1 if unset
	int entity_index; // -1 if unset
	int light_index;  // -1 if unset
	float x;          // NAN if unset
	float y;          // NAN if unset
} MapValidationContext;

typedef struct MapValidationEntry {
	const char* code; // stable string
	char* message;    // owned; freed by map_validation_report_destroy
	MapValidationContext context;
} MapValidationEntry;

typedef struct MapValidationReport {
	MapValidationEntry* errors;
	int error_count;
	int error_cap;
	MapValidationEntry* warnings;
	int warning_count;
	int warning_cap;
} MapValidationReport;

void map_validation_report_init(MapValidationReport* out);
void map_validation_report_destroy(MapValidationReport* report);

// Optional sink for structured diagnostics.
// When set (non-NULL), map_validate will append warnings/errors in deterministic order.
// When unset, behavior remains logging-only.
void map_validate_set_report_sink(MapValidationReport* report);

bool map_validate(const World* world, float player_start_x, float player_start_y, const struct MapDoor* doors, int door_count);
