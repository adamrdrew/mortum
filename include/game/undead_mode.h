#pragma once

#include "game/entity.h"
#include "game/player.h"

// Handles Mortum->Undead transitions, health drain, and recovery via shards.
void undead_mode_update(Player* player, EntityList* entities, double dt_s);
