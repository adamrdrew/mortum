#include "game/episode_runner.h"

#include "game/world.h"

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
	player->body.x = map->player_start_x;
	player->body.y = map->player_start_y;
	player->body.vx = 0.0f;
	player->body.vy = 0.0f;
	player->body.vz = 0.0f;
	player->body.step_up.active = false;
	player->body.on_ground = true;

	// Initialize z to the start sector floor (best-effort).
	player->body.sector = -1;
	player->body.last_valid_sector = -1;
	int s = world_find_sector_at_point(&map->world, player->body.x, player->body.y);
	if ((unsigned)s < (unsigned)map->world.sector_count) {
		player->body.sector = s;
		player->body.last_valid_sector = s;
		player->body.z = map->world.sectors[s].floor_z;
	} else {
		player->body.z = 0.0f;
		player->body.on_ground = false;
	}
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
