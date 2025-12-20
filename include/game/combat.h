#pragma once

#include "game/entity.h"
#include "game/player.h"

void combat_damage_player(Player* player, int damage);
void combat_damage_entity(Entity* entity, int damage);
