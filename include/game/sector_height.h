#pragma once

#include <stdbool.h>

#include "game/player.h"
#include "game/world.h"
#include "game/sound_emitters.h"
#include "game/notifications.h"

// Attempts to complete the level when the player is touching a wall with
// end_level=true. Returns true if the trigger fired.
bool sector_height_try_end_level_touching_wall(World* world, Player* player, float now_s);

// Attempts to toggle a movable sector when the player is touching a wall with
// toggle_sector=true. Returns true if a toggle was started.
bool sector_height_try_toggle_touching_wall(
	World* world,
	Player* player,
	SoundEmitters* sfx,
	Notifications* notifications,
	float listener_x,
	float listener_y,
	float now_s);

// Advances any active sector floor movements. Applies safeguards to keep the
// player stable on moving floors and prevent ceiling clipping.
void sector_height_update(World* world, Player* player, SoundEmitters* sfx, float listener_x, float listener_y, double dt_s);
