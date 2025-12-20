#include "game/dash.h"

#include "game/collision.h"

#include <math.h>

static float clamp_min0(float v) {
	return v < 0.0f ? 0.0f : v;
}

bool dash_update(Player* player, const World* world, bool dash_down, float radius, float dir_x, float dir_y, double dt_s) {
	if (!player) {
		return false;
	}

	// Cooldown tick
	player->dash_cooldown_s = clamp_min0(player->dash_cooldown_s - (float)dt_s);

	// Rising edge detect
	bool pressed = dash_down && !player->dash_prev_down;
	player->dash_prev_down = dash_down;

	if (!pressed) {
		return false;
	}
	if (player->dash_cooldown_s > 0.0f) {
		return false;
	}

	float len = sqrtf(dir_x * dir_x + dir_y * dir_y);
	if (len < 1e-6f) {
		return false;
	}
	dir_x /= len;
	dir_y /= len;

	// Tunables (US1): short, learnable quick-step.
	const float dash_distance = 0.85f;
	const float dash_cooldown_s = 0.65f;

	float to_x = player->x + dir_x * dash_distance;
	float to_y = player->y + dir_y * dash_distance;

	if (world) {
		CollisionMoveResult r = collision_move_circle(world, radius, player->x, player->y, to_x, to_y);
		player->x = r.out_x;
		player->y = r.out_y;
	} else {
		player->x = to_x;
		player->y = to_y;
	}

	player->dash_cooldown_s = dash_cooldown_s;
	return true;
}
