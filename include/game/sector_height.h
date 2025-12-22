#pragma once

#include <stdbool.h>

#include "game/player.h"
#include "game/world.h"

// Attempts to toggle a movable sector when the player is touching a wall with
// toggle_sector=true. Returns true if a toggle was started.
bool sector_height_try_toggle_touching_wall(World* world, Player* player);

// Advances any active sector floor movements. Applies safeguards to keep the
// player stable on moving floors and prevent ceiling clipping.
void sector_height_update(World* world, Player* player, double dt_s);
