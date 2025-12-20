#pragma once

#include <stdbool.h>

#include "game/entity.h"
#include "game/player.h"

bool exit_check_reached(const Player* player, const EntityList* entities);
