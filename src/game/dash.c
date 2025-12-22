#include "game/dash.h"

#include "game/physics_body.h"

#include <math.h>

static float clamp_min0(float v) {
	return v < 0.0f ? 0.0f : v;
}

bool dash_update(Player* player, const World* world, bool dash_down, float dir_x, float dir_y, double dt_s, const PhysicsBodyParams* params) {
	if (!player) {
		return false;
	}
	if (!params) {
		PhysicsBodyParams tmp = physics_body_params_default();
		params = &tmp;
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

	physics_body_move_delta(&player->body, world, dir_x * dash_distance, dir_y * dash_distance, params);

	player->dash_cooldown_s = dash_cooldown_s;
	return true;
}
