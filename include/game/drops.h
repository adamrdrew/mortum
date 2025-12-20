#pragma once

#include "game/entity.h"
#include "game/player.h"

// Spawns shard pickups from recently-killed enemies during Undead mode.
void drops_update(Player* player, EntityList* entities);
