#pragma once

#include <stdbool.h>

#include "game/entity.h"
#include "game/player.h"
#include "game/world.h"

void debug_spawn_enemy(Player* player, const World* world, EntityList* entities);
