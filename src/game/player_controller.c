#include "game/player_controller.h"

#include "game/dash.h"
#include "game/physics_body.h"

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
	if (dt_s <= 0.0) {
		return;
	}

	float x0 = player->body.x;
	float y0 = player->body.y;

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

	const float move_speed = 4.7f; // units per second
	float ang = player->angle_deg * (float)M_PI / 180.0f;
	float fx = cosf(ang);
	float fy = sinf(ang);
	float rx = -fy;
	float ry = fx;

	PhysicsBodyParams phys = physics_body_params_default();

	if (!player->noclip) {
		// Dash/quick-step: impulse-like move with cooldown.
		// Uses movement direction when provided; otherwise dashes forward.
		float dir_x = fx * forward + rx * right;
		float dir_y = fy * forward + ry * right;
		if (fabsf(dir_x) < 1e-6f && fabsf(dir_y) < 1e-6f) {
			dir_x = fx;
			dir_y = fy;
		}
		(void)dash_update(player, world, in->dash, dir_x, dir_y, dt_s, &phys);
	}

	float vx = (fx * forward + rx * right) * move_speed;
	float vy = (fy * forward + ry * right) * move_speed;

	if (player->noclip || !world) {
		player->body.x += vx * (float)dt_s;
		player->body.y += vy * (float)dt_s;
		player->body.vx = (player->body.x - x0) / (float)dt_s;
		player->body.vy = (player->body.y - y0) / (float)dt_s;

		float speed = sqrtf(player->body.vx * player->body.vx + player->body.vy * player->body.vy);
		float target_amp = speed / move_speed;
		if (target_amp < 0.0f) {
			target_amp = 0.0f;
		}
		if (target_amp > 1.0f) {
			target_amp = 1.0f;
		}
		float smooth = (float)(dt_s * 8.0);
		if (smooth > 1.0f) {
			smooth = 1.0f;
		}
		player->weapon_view_bob_amp += (target_amp - player->weapon_view_bob_amp) * smooth;
		player->weapon_view_bob_phase += (float)(dt_s * 10.0) * (0.2f + 0.8f * player->weapon_view_bob_amp);
		return;
	}

	physics_body_update(&player->body, world, vx, vy, dt_s, &phys);

	// Weapon view bob inputs.
	float speed = sqrtf(player->body.vx * player->body.vx + player->body.vy * player->body.vy);
	float target_amp = speed / move_speed;
	if (target_amp < 0.0f) {
		target_amp = 0.0f;
	}
	if (target_amp > 1.0f) {
		target_amp = 1.0f;
	}
	float smooth = (float)(dt_s * 8.0);
	if (smooth > 1.0f) {
		smooth = 1.0f;
	}
	player->weapon_view_bob_amp += (target_amp - player->weapon_view_bob_amp) * smooth;
	player->weapon_view_bob_phase += (float)(dt_s * 10.0) * (0.2f + 0.8f * player->weapon_view_bob_amp);
}
