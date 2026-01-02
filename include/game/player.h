#pragma once

#include "core/base.h"

#include "game/ammo.h"
#include "game/inventory.h"
#include "game/weapon_defs.h"
#include "game/physics_body.h"

typedef struct Player {
	PhysicsBody body;
	float angle_deg;
	int health;
	int health_max;
	AmmoState ammo;
	uint32_t weapons_owned_mask;
	WeaponId weapon_equipped;
	float weapon_view_bob_phase;
	float weapon_view_bob_amp;
	bool weapon_view_anim_shooting;
	int weapon_view_anim_frame;
	float weapon_view_anim_t;
	uint8_t weapon_select_prev_mask;
	int mortum_pct;
	int keys;
	int purge_items;
	Inventory inventory;
	bool use_prev_down;
	bool undead_active;
	int undead_shards_required;
	int undead_shards_collected;
	float weapon_cooldown_s;
	float dash_cooldown_s;
	bool dash_prev_down;
	bool noclip;
	bool noclip_prev_down;
	bool action_prev_down;
	float footstep_timer_s;
	uint8_t footstep_variant;
} Player;

void player_init(Player* p);
