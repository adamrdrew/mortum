#pragma once

#include "game/entity.h"
#include "game/player.h"

// Applies pickup effects and deactivates collected pickup entities.
void pickups_update(Player* player, EntityList* entities);
