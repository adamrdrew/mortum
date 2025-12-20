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
	EntityList* entities,
	bool fire_down,
	int weapon_wheel_delta,
	uint8_t weapon_select_mask,
	double dt_s) {
	(void)world;
	if (!player || !entities || dt_s <= 0.0) {
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

	float base = player->angle_deg * (float)M_PI / 180.0f;
	int pellets = def->pellets > 0 ? def->pellets : 1;
	float spread_rad = def->spread_deg * (float)M_PI / 180.0f;
	float start = (pellets == 1) ? 0.0f : (-0.5f * spread_rad);
	float step = (pellets == 1) ? 0.0f : (spread_rad / (float)(pellets - 1));

	bool any_spawned = false;
	for (int i = 0; i < pellets; i++) {
		float ang = base + start + step * (float)i;
		float dx = cosf(ang);
		float dy = sinf(ang);

		Entity proj;
		entity_init(&proj);
		strncpy(proj.type, "proj_player", sizeof(proj.type) - 1);
		proj.type[sizeof(proj.type) - 1] = '\0';
		proj.x = player->x + dx * 0.10f;
		proj.y = player->y + dy * 0.10f;
		proj.z = 0.0f;
		proj.vx = dx * def->proj_speed;
		proj.vy = dy * def->proj_speed;
		proj.radius = def->proj_radius;
		proj.lifetime_s = def->proj_life_s;
		proj.damage = def->proj_damage;

		if (entity_list_push(entities, &proj)) {
			any_spawned = true;
		}
	}

	if (!any_spawned) {
		// Refund ammo if nothing could be spawned.
		(void)ammo_add(&player->ammo, def->ammo_type, def->ammo_per_shot);
		return;
	}

	player->weapon_view_anim_shooting = true;
	player->weapon_view_anim_frame = 0;
	player->weapon_view_anim_t = 0.0f;
	player->weapon_cooldown_s = def->shot_cooldown_s;
}
