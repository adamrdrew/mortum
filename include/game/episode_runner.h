#pragma once

#include <stdbool.h>

#include "assets/episode_loader.h"
#include "assets/map_loader.h"
#include "game/player.h"

typedef struct EpisodeRunner {
	int map_index;
} EpisodeRunner;

void episode_runner_init(EpisodeRunner* self);

// Starts episode progression at the first map.
bool episode_runner_start(EpisodeRunner* self, const Episode* ep);

// Returns current map filename (relative to Assets/Levels/), or NULL.
const char* episode_runner_current_map(const EpisodeRunner* self, const Episode* ep);

// Advances to next map. Returns false if no next map exists.
bool episode_runner_advance(EpisodeRunner* self, const Episode* ep);

// Applies per-level reset while preserving loadout/upgrades.
void episode_runner_apply_level_start(Player* player, const MapLoadResult* map);
