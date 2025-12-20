#include "game/player_controller.h"

#include "game/collision.h"
#include "game/dash.h"

#include <math.h>

static float wrap_deg(float a) {
	while (a >= 360.0f) {
		a -= 360.0f;
	}
	while (a < 0.0f) {
		a += 360.0f;
	}
	return a;
}

void player_controller_update(Player* player, const World* world, const PlayerControllerInput* in, double dt_s) {
	if (!player || !in) {
		return;
	}

	// Mouse look (yaw)
	const float mouse_sens_deg_per_px = 0.12f;
	player->angle_deg = wrap_deg(player->angle_deg + (float)in->mouse_dx * mouse_sens_deg_per_px);

	// WASD movement (strafe + forward)
	float forward = 0.0f;
	float right = 0.0f;
	if (in->forward) {
		forward += 1.0f;
	}
	if (in->back) {
		forward -= 1.0f;
	}
	if (in->right) {
		right += 1.0f;
	}
	if (in->left) {
		right -= 1.0f;
	}

	float len = sqrtf(forward * forward + right * right);
	if (len > 1e-6f) {
		forward /= len;
		right /= len;
	}

	const float move_speed = 2.5f; // units per second
	float ang = player->angle_deg * (float)M_PI / 180.0f;
	float fx = cosf(ang);
	float fy = sinf(ang);
	float rx = -fy;
	float ry = fx;

	// Collision resolve (circle radius tuned for corridors)
	const float radius = 0.20f;

	if (!player->noclip) {
		// Dash/quick-step: impulse-like move with cooldown.
		// Uses movement direction when provided; otherwise dashes forward.
		float dir_x = fx * forward + rx * right;
		float dir_y = fy * forward + ry * right;
		if (fabsf(dir_x) < 1e-6f && fabsf(dir_y) < 1e-6f) {
			dir_x = fx;
			dir_y = fy;
		}
		(void)dash_update(player, world, in->dash, radius, dir_x, dir_y, dt_s);
	}

	float vx = (fx * forward + rx * right) * move_speed;
	float vy = (fy * forward + ry * right) * move_speed;

	float to_x = player->x + vx * (float)dt_s;
	float to_y = player->y + vy * (float)dt_s;

	if (player->noclip || !world) {
		player->x = to_x;
		player->y = to_y;
		return;
	}

	CollisionMoveResult r = collision_move_circle(world, radius, player->x, player->y, to_x, to_y);
	player->x = r.out_x;
	player->y = r.out_y;
}
