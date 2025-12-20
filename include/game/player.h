#pragma once

#include "core/base.h"

#include "game/ammo.h"
#include "game/weapon_defs.h"

typedef struct Player {
	float x;
	float y;
	float angle_deg;
	int health;
	int health_max;
	AmmoState ammo;
	uint32_t weapons_owned_mask;
	WeaponId weapon_equipped;
	uint8_t weapon_select_prev_mask;
	int mortum_pct;
	int keys;
	int purge_items;
	bool use_prev_down;
	bool undead_active;
	int undead_shards_required;
	int undead_shards_collected;
	float weapon_cooldown_s;
	float dash_cooldown_s;
	bool dash_prev_down;
	bool noclip;
	bool noclip_prev_down;
} Player;

void player_init(Player* p);
