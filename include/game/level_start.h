#pragma once

#include "assets/map_loader.h"

#include "game/player.h"

// Applies per-level reset while preserving loadout/upgrades.
void level_start_apply(Player* player, const MapLoadResult* map);
