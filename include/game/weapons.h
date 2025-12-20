#pragma once

#include <stdbool.h>

#include "game/entity.h"
#include "game/player.h"
#include "game/world.h"

// Basic US1 sidearm: spawns player projectiles while fire is held.
// US3: Supports multiple weapons (1-4 select) and mouse wheel cycling.
// `weapon_select_mask`: bit0..bit3 correspond to 1..4 keys held this frame.
void weapons_update(
	Player* player,
	const World* world,
	EntityList* entities,
	bool fire_down,
	int weapon_wheel_delta,
	uint8_t weapon_select_mask,
	double dt_s);
