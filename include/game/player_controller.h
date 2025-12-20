#pragma once

#include <stdbool.h>

#include "game/player.h"
#include "game/world.h"

typedef struct PlayerControllerInput {
	bool forward;
	bool back;
	bool left;
	bool right;
	bool dash;
	int mouse_dx;
} PlayerControllerInput;

void player_controller_update(Player* player, const World* world, const PlayerControllerInput* in, double dt_s);
