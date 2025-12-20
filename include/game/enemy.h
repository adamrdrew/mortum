#pragma once

#include "game/entity.h"
#include "game/player.h"
#include "game/world.h"

// Updates enemies (movement/attacks) and can spawn enemy projectiles.
void enemy_update(Player* player, const World* world, EntityList* entities, double dt_s);
