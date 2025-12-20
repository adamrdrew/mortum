#include "game/weapon_defs.h"

static const WeaponDef k_defs[WEAPON_COUNT] = {
	{
		.id = WEAPON_SIDEARM,
		.name = "Sidearm",
		.ammo_type = AMMO_BULLETS,
		.ammo_per_shot = 1,
		.shot_cooldown_s = 0.18f,
		.pellets = 1,
		.spread_deg = 0.0f,
		.proj_speed = 7.0f,
		.proj_radius = 0.055f,
		.proj_life_s = 1.2f,
		.proj_damage = 25,
	},
	{
		.id = WEAPON_SHOTGUN,
		.name = "Shotgun",
		.ammo_type = AMMO_SHELLS,
		.ammo_per_shot = 1,
		.shot_cooldown_s = 0.70f,
		.pellets = 6,
		.spread_deg = 14.0f,
		.proj_speed = 6.2f,
		.proj_radius = 0.050f,
		.proj_life_s = 0.75f,
		.proj_damage = 16,
	},
	{
		.id = WEAPON_RIFLE,
		.name = "Rifle",
		.ammo_type = AMMO_BULLETS,
		.ammo_per_shot = 2,
		.shot_cooldown_s = 0.28f,
		.pellets = 1,
		.spread_deg = 1.5f,
		.proj_speed = 9.5f,
		.proj_radius = 0.045f,
		.proj_life_s = 1.4f,
		.proj_damage = 40,
	},
	{
		.id = WEAPON_CROWDCONTROL,
		.name = "Chaingun",
		.ammo_type = AMMO_CELLS,
		.ammo_per_shot = 1,
		.shot_cooldown_s = 0.08f,
		.pellets = 1,
		.spread_deg = 6.0f,
		.proj_speed = 7.8f,
		.proj_radius = 0.040f,
		.proj_life_s = 1.0f,
		.proj_damage = 14,
	},
};

const WeaponDef* weapon_def_get(WeaponId id) {
	if ((int)id < 0 || id >= WEAPON_COUNT) {
		return &k_defs[0];
	}
	return &k_defs[(int)id];
}
