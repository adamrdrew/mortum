#include "game/weapons.h"

#include "game/ammo.h"
#include "game/weapon_defs.h"

#include <math.h>
#include <string.h>

static float clamp_min0(float v) {
	return v < 0.0f ? 0.0f : v;
}

static bool weapon_is_owned(const Player* player, WeaponId id) {
	if (!player) {
		return false;
	}
	return (player->weapons_owned_mask & (1u << (unsigned)id)) != 0;
}

static WeaponId weapon_next_owned(const Player* player, WeaponId cur, int dir) {
	if (!player) {
		return cur;
	}
	if (dir == 0) {
		return cur;
	}
	int start = (int)cur;
	for (int step = 0; step < (int)WEAPON_COUNT; step++) {
		start = (start + (dir > 0 ? 1 : -1) + (int)WEAPON_COUNT) % (int)WEAPON_COUNT;
		WeaponId cand = (WeaponId)start;
		if (weapon_is_owned(player, cand)) {
			return cand;
		}
	}
	return cur;
}

void weapons_update(
	Player* player,
	const World* world,
	SoundEmitters* sfx,
	float listener_x,
	float listener_y,
	bool fire_down,
	int weapon_wheel_delta,
	uint8_t weapon_select_mask,
	double dt_s) {
	(void)world;
	(void)listener_x;
	(void)listener_y;
	if (!player || dt_s <= 0.0) {
		return;
	}

	// Step weapon view shoot animation.
	if (player->weapon_view_anim_shooting) {
		player->weapon_view_anim_t += (float)dt_s;
		const float frame_dt = 1.0f / 30.0f;
		while (player->weapon_view_anim_t >= frame_dt) {
			player->weapon_view_anim_t -= frame_dt;
			player->weapon_view_anim_frame += 1;
			if (player->weapon_view_anim_frame >= 6) {
				player->weapon_view_anim_shooting = false;
				player->weapon_view_anim_frame = 0;
				player->weapon_view_anim_t = 0.0f;
				break;
			}
		}
	}

	WeaponId weapon_before = player->weapon_equipped;

	// Switching (edge-triggered on number keys; wheel cycles continuously).
	uint8_t pressed = (uint8_t)(weapon_select_mask & (uint8_t)~player->weapon_select_prev_mask);
	player->weapon_select_prev_mask = weapon_select_mask;
	if (pressed != 0) {
		for (int i = 0; i < (int)WEAPON_COUNT && i < 8; i++) {
			if ((pressed & (uint8_t)(1u << (unsigned)i)) == 0) {
				continue;
			}
			WeaponId want = (WeaponId)i;
			if (weapon_is_owned(player, want)) {
				player->weapon_equipped = want;
				break;
			}
		}
	}
	if (weapon_wheel_delta != 0) {
		int dir = weapon_wheel_delta > 0 ? 1 : -1;
		int steps = weapon_wheel_delta > 0 ? weapon_wheel_delta : -weapon_wheel_delta;
		for (int i = 0; i < steps; i++) {
			player->weapon_equipped = weapon_next_owned(player, player->weapon_equipped, dir);
		}
	}

	if (player->weapon_equipped != weapon_before) {
		player->weapon_view_anim_shooting = false;
		player->weapon_view_anim_frame = 0;
		player->weapon_view_anim_t = 0.0f;
	}

	player->weapon_cooldown_s = clamp_min0(player->weapon_cooldown_s - (float)dt_s);
	if (!fire_down) {
		return;
	}
	if (player->weapon_cooldown_s > 0.0f) {
		return;
	}

	const WeaponDef* def = weapon_def_get(player->weapon_equipped);
	if (!def) {
		return;
	}
	if (!ammo_consume(&player->ammo, def->ammo_type, def->ammo_per_shot)) {
		return;
	}

	player->weapon_view_anim_shooting = true;
	player->weapon_view_anim_frame = 0;
	player->weapon_view_anim_t = 0.0f;
	player->weapon_cooldown_s = def->shot_cooldown_s;

	// SFX: generic gunshot emitted from player/camera position (non-spatial).
	if (sfx) {
		const char* wav = "Shotgun_Shot-001.wav";
		switch (player->weapon_equipped) {
			case WEAPON_SHOTGUN: wav = "Shotgun_Shot-001.wav"; break;
			case WEAPON_ROCKET: wav = "Rocket_Shot-001.wav"; break;
			case WEAPON_HANDGUN: wav = "Sniper_Shot-001.wav"; break;
			case WEAPON_RIFLE: wav = "Sniper_Shot-002.wav"; break;
			case WEAPON_SMG: wav = "Sniper_Shot-003.wav"; break;
			default: break;
		}
		sound_emitters_play_one_shot_at(sfx, wav, player->body.x, player->body.y, false, 1.0f, listener_x, listener_y);
	}
}
