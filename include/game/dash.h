#pragma once

#include <stdbool.h>

#include "game/player.h"
#include "game/world.h"

// Updates dash cooldown and (on a rising-edge dash press) applies an impulse-like
// quick-step move along (dir_x, dir_y), resolved through collision.
//
// Returns true if a dash was performed this tick.
bool dash_update(Player* player, const World* world, bool dash_down, float radius, float dir_x, float dir_y, double dt_s);
