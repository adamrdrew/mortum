#pragma once

#include "game/entity.h"
#include "game/player.h"

// Increases Mortum from hazard entities based on proximity.
void corruption_update(Player* player, const EntityList* entities, double dt_s);
