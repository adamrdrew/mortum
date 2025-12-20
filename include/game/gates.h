#pragma once

#include "game/entity.h"
#include "game/player.h"

// Key-door gate. If player has a key, gate deactivates.
// While active, gate blocks player movement as a circular obstacle.
void gates_update_and_resolve(Player* player, EntityList* entities);
