#pragma once

#include "game/player.h"

// Adds delta to mortum_pct and clamps to [0,100].
void mortum_add(Player* player, int delta);

// Sets mortum_pct to value clamped to [0,100].
void mortum_set(Player* player, int value);
