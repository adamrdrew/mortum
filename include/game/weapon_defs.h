#pragma once

#include "game/ammo.h"

typedef enum WeaponId {
	WEAPON_SIDEARM = 0,
	WEAPON_SHOTGUN = 1,
	WEAPON_RIFLE = 2,
	WEAPON_CROWDCONTROL = 3,
	WEAPON_COUNT
} WeaponId;

typedef struct WeaponDef {
	WeaponId id;
	const char* name;
	AmmoType ammo_type;
	int ammo_per_shot;
	float shot_cooldown_s;
	int pellets;
	float spread_deg;
	float proj_speed;
	float proj_radius;
	float proj_life_s;
	int proj_damage;
} WeaponDef;

const WeaponDef* weapon_def_get(WeaponId id);
