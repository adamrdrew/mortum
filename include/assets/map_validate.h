#pragma once

#include <stdbool.h>

#include "game/world.h"

bool map_validate(const World* world, float player_start_x, float player_start_y);
