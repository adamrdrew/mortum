#pragma once

#include <stdio.h>

#include "game/player.h"
#include "game/world.h"
#include "render/camera.h"

// Prints a one-shot debug snapshot for investigating visual issues.
// Intended to be called only on-demand (e.g. keypress).
void debug_dump_print(FILE* out, const char* map_name, const World* world, const Player* player, const Camera* cam);
