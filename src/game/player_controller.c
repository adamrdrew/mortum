#include "game/player_controller.h"

#include "core/config.h"

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

	const CoreConfig* cfg = core_config_get();
	const float mouse_sens_deg_per_px = cfg ? cfg->player.mouse_sens_deg_per_px : 0.12f;
	const float move_speed = cfg ? cfg->player.move_speed : 4.7f;
	const float run_speed_multiplier = cfg ? cfg->player.run_speed_multiplier : 1.45f;
	const float run_bob_phase_multiplier = cfg ? cfg->player.run_bob_phase_multiplier : 1.25f;
	const float bob_smooth_rate = cfg ? cfg->player.weapon_view_bob_smooth_rate : 8.0f;
	const float bob_phase_rate = cfg ? cfg->player.weapon_view_bob_phase_rate : 10.0f;
	const float bob_phase_base = cfg ? cfg->player.weapon_view_bob_phase_base : 0.2f;
	const float bob_phase_amp = cfg ? cfg->player.weapon_view_bob_phase_amp : 0.8f;

	// Mouse look (yaw)
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
	const bool moving = len > 1e-6f;
	const bool running = in->dash && moving;
	const float speed_multiplier = running ? run_speed_multiplier : 1.0f;
	const float bob_phase_multiplier = running ? run_bob_phase_multiplier : 1.0f;

	float ang = player->angle_deg * (float)M_PI / 180.0f;
	float fx = cosf(ang);
	float fy = sinf(ang);
	float rx = -fy;
	float ry = fx;

	PhysicsBodyParams phys = physics_body_params_default();

	float vx = (fx * forward + rx * right) * move_speed * speed_multiplier;
	float vy = (fy * forward + ry * right) * move_speed * speed_multiplier;

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
		float smooth = (float)(dt_s * bob_smooth_rate);
		if (smooth > 1.0f) {
			smooth = 1.0f;
		}
		player->weapon_view_bob_amp += (target_amp - player->weapon_view_bob_amp) * smooth;
		player->weapon_view_bob_phase += (float)(dt_s * bob_phase_rate * bob_phase_multiplier) * (bob_phase_base + bob_phase_amp * player->weapon_view_bob_amp);
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
	float smooth = (float)(dt_s * bob_smooth_rate);
	if (smooth > 1.0f) {
		smooth = 1.0f;
	}
	player->weapon_view_bob_amp += (target_amp - player->weapon_view_bob_amp) * smooth;
	player->weapon_view_bob_phase += (float)(dt_s * bob_phase_rate * bob_phase_multiplier) * (bob_phase_base + bob_phase_amp * player->weapon_view_bob_amp);
}
