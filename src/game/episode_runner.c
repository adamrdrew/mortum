#include "game/episode_runner.h"

#include <string.h>

void episode_runner_init(EpisodeRunner* self) {
	if (!self) {
		return;
	}
	self->map_index = 0;
}

bool episode_runner_start(EpisodeRunner* self, const Episode* ep) {
	if (!self || !ep || !ep->maps || ep->map_count <= 0) {
		return false;
	}
	self->map_index = 0;
	return true;
}

const char* episode_runner_current_map(const EpisodeRunner* self, const Episode* ep) {
	if (!self || !ep || !ep->maps || ep->map_count <= 0) {
		return NULL;
	}
	if (self->map_index < 0 || self->map_index >= ep->map_count) {
		return NULL;
	}
	return ep->maps[self->map_index];
}

bool episode_runner_advance(EpisodeRunner* self, const Episode* ep) {
	if (!self || !ep || ep->map_count <= 0) {
		return false;
	}
	int next = self->map_index + 1;
	if (next < 0 || next >= ep->map_count) {
		return false;
	}
	self->map_index = next;
	return true;
}

void episode_runner_apply_level_start(Player* player, const MapLoadResult* map) {
	if (!player || !map) {
		return;
	}
	player->x = map->player_start_x;
	player->y = map->player_start_y;
	player->angle_deg = map->player_start_angle_deg;

	// Per-level resets
	player->keys = 0;
	player->weapon_cooldown_s = 0.0f;
	player->dash_cooldown_s = 0.0f;
	player->dash_prev_down = false;

	// Clear undead session state on new level start.
	player->undead_active = false;
	player->undead_shards_required = 0;
	player->undead_shards_collected = 0;
	player->use_prev_down = false;
	player->weapon_select_prev_mask = 0;
}
