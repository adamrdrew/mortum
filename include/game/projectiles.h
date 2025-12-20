#pragma once

#include <stdbool.h>

#include "game/entity.h"
#include "game/player.h"
#include "game/world.h"

// Updates projectile movement, wall collision, and applies damage on hit.
void projectiles_update(Player* player, const World* world, EntityList* entities, double dt_s);
