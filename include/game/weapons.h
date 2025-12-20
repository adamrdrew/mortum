#pragma once

#include <stdbool.h>

#include "game/entity.h"
#include "game/player.h"
#include "game/world.h"

// Basic US1: spawns player projectiles while fire is held.
// US3: Supports multiple weapons (1-5 select) and cycling (mouse wheel and/or Q/E via wheel delta).
// `weapon_select_mask`: bit0..bit4 correspond to 1..5 keys held this frame.
void weapons_update(
	Player* player,
	const World* world,
	EntityList* entities,
	bool fire_down,
	int weapon_wheel_delta,
	uint8_t weapon_select_mask,
	double dt_s);
