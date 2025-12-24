#pragma once

#include <stdio.h>

#include "game/entities.h"
#include "game/player.h"
#include "game/world.h"
#include "render/camera.h"

// Prints a one-shot debug snapshot for investigating visual issues.
// Intended to be called only on-demand (e.g. keypress).
void debug_dump_print(FILE* out, const char* map_name, const World* world, const Player* player, const Camera* cam);

// Prints a one-shot detailed entity snapshot for investigating render/physics issues.
// Includes world state and the projected screen placement used by sprite rendering.
void debug_dump_print_entities(
	FILE* out,
	const char* map_name,
	const World* world,
	const Player* player,
	const Camera* cam,
	const EntitySystem* entities,
	int fb_width,
	int fb_height,
	const float* wall_depth
);
