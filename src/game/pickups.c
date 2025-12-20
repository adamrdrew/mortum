#include "game/pickups.h"

#include "game/ammo.h"
#include "game/upgrades.h"
#include "game/weapon_defs.h"

#include <math.h>
#include <string.h>

static float dist2(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

static int imin(int a, int b) {
	return a < b ? a : b;
}

void pickups_update(Player* player, EntityList* entities) {
	if (!player || !entities) {
		return;
	}

	const float pickup_r2 = 0.45f * 0.45f;
	for (int i = 0; i < entities->count; i++) {
		Entity* e = &entities->items[i];
		if (!e->active) {
			continue;
		}
		if (strncmp(e->type, "pickup_", 7) != 0) {
			continue;
		}
		if (dist2(player->x, player->y, e->x, e->y) > pickup_r2) {
			continue;
		}

		if (strcmp(e->type, "pickup_health") == 0) {
			player->health = imin(player->health_max, player->health + 25);
			e->active = false;
		} else if (strcmp(e->type, "pickup_ammo") == 0) {
			(void)ammo_add(&player->ammo, AMMO_BULLETS, 10);
			e->active = false;
		} else if (strcmp(e->type, "pickup_ammo_bullets") == 0) {
			(void)ammo_add(&player->ammo, AMMO_BULLETS, 20);
			e->active = false;
		} else if (strcmp(e->type, "pickup_ammo_shells") == 0) {
			(void)ammo_add(&player->ammo, AMMO_SHELLS, 6);
			e->active = false;
		} else if (strcmp(e->type, "pickup_ammo_cells") == 0) {
			(void)ammo_add(&player->ammo, AMMO_CELLS, 25);
			e->active = false;
		} else if (strcmp(e->type, "pickup_toxic_medkit") == 0) {
			player->purge_items += 1;
			e->active = false;
		} else if (strcmp(e->type, "pickup_mortum_small") == 0) {
			player->mortum_pct = imin(100, player->mortum_pct + 15);
			e->active = false;
		} else if (strcmp(e->type, "pickup_mortum_large") == 0) {
			player->mortum_pct = imin(100, player->mortum_pct + 35);
			e->active = false;
		} else if (strcmp(e->type, "pickup_shard") == 0) {
			player->undead_shards_collected += 1;
			e->active = false;
		} else if (strcmp(e->type, "pickup_weapon_handgun") == 0 || strcmp(e->type, "pickup_weapon_sidearm") == 0) {
			player->weapons_owned_mask |= (1u << (unsigned)WEAPON_HANDGUN);
			player->weapon_equipped = WEAPON_HANDGUN;
			(void)ammo_add(&player->ammo, AMMO_BULLETS, 15);
			e->active = false;
		} else if (strcmp(e->type, "pickup_weapon_shotgun") == 0) {
			player->weapons_owned_mask |= (1u << (unsigned)WEAPON_SHOTGUN);
			player->weapon_equipped = WEAPON_SHOTGUN;
			(void)ammo_add(&player->ammo, AMMO_SHELLS, 8);
			e->active = false;
		} else if (strcmp(e->type, "pickup_weapon_rifle") == 0) {
			player->weapons_owned_mask |= (1u << (unsigned)WEAPON_RIFLE);
			player->weapon_equipped = WEAPON_RIFLE;
			(void)ammo_add(&player->ammo, AMMO_BULLETS, 20);
			e->active = false;
		} else if (strcmp(e->type, "pickup_weapon_smg") == 0 || strcmp(e->type, "pickup_weapon_chaingun") == 0) {
			player->weapons_owned_mask |= (1u << (unsigned)WEAPON_SMG);
			player->weapon_equipped = WEAPON_SMG;
			(void)ammo_add(&player->ammo, AMMO_BULLETS, 30);
			e->active = false;
		} else if (strcmp(e->type, "pickup_weapon_rocket") == 0) {
			player->weapons_owned_mask |= (1u << (unsigned)WEAPON_ROCKET);
			player->weapon_equipped = WEAPON_ROCKET;
			(void)ammo_add(&player->ammo, AMMO_CELLS, 20);
			e->active = false;
		} else if (strcmp(e->type, "pickup_upgrade_max_health") == 0) {
			(void)upgrades_apply_max_health(player);
			e->active = false;
		} else if (strcmp(e->type, "pickup_upgrade_max_ammo") == 0) {
			(void)upgrades_apply_max_ammo(player);
			e->active = false;
		} else if (strcmp(e->type, "pickup_key") == 0) {
			player->keys |= 1;
			e->active = false;
		}
	}
}
