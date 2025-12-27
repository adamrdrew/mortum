#include "game/level_start.h"

#include "game/world.h"

void level_start_apply(Player* player, const MapLoadResult* map) {
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

	// Initialize z to the start sector floor.
	// NOTE: Some maps intentionally have nested/overlapping sectors (e.g. a raised platform inside a room).
	// A pure point-in-polygon search can match multiple sectors; for spawn we pick the highest floor the
	// body fits under.
	player->body.sector = -1;
	player->body.last_valid_sector = -1;
	int best = -1;
	float best_floor = -1e30f;
	for (int i = 0; i < map->world.sector_count; i++) {
		if (!world_sector_contains_point(&map->world, i, player->body.x, player->body.y)) {
			continue;
		}
		const Sector* s = &map->world.sectors[i];
		// Conservative headroom check (mirrors physics epsilon).
		if (s->floor_z + player->body.height > s->ceil_z - 0.08f) {
			continue;
		}
		if (best < 0 || s->floor_z > best_floor) {
			best = i;
			best_floor = s->floor_z;
		}
	}
	if (best < 0) {
		best = world_find_sector_at_point(&map->world, player->body.x, player->body.y);
	}
	if ((unsigned)best < (unsigned)map->world.sector_count) {
		player->body.sector = best;
		player->body.last_valid_sector = best;
		player->body.z = map->world.sectors[best].floor_z;
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
