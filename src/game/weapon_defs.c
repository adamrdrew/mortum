#include "game/weapon_defs.h"

#include "core/config.h"

#include <stddef.h>

static const WeaponDef k_defs[WEAPON_COUNT] = {
	{
		.id = WEAPON_HANDGUN,
		.name = "Handgun",
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
		.id = WEAPON_SMG,
		.name = "SMG",
		.ammo_type = AMMO_BULLETS,
		.ammo_per_shot = 1,
		.shot_cooldown_s = 0.08f,
		.pellets = 1,
		.spread_deg = 6.0f,
		.proj_speed = 7.8f,
		.proj_radius = 0.040f,
		.proj_life_s = 1.0f,
		.proj_damage = 14,
	},
	{
		.id = WEAPON_ROCKET,
		.name = "Rocket",
		.ammo_type = AMMO_CELLS,
		.ammo_per_shot = 4,
		.shot_cooldown_s = 0.95f,
		.pellets = 1,
		.spread_deg = 0.0f,
		.proj_speed = 5.5f,
		.proj_radius = 0.090f,
		.proj_life_s = 1.8f,
		.proj_damage = 120,
	},
};

const WeaponDef* weapon_def_get(WeaponId id) {
	if ((int)id < 0 || id >= WEAPON_COUNT) {
		return &k_defs[0];
	}
	static WeaponDef tmp;
	tmp = k_defs[(int)id];

	const CoreConfig* cfg = core_config_get();
	if (!cfg) {
		return &tmp;
	}
	const WeaponBalanceConfig* b = NULL;
	switch (id) {
		case WEAPON_HANDGUN: b = &cfg->weapons.handgun; break;
		case WEAPON_SHOTGUN: b = &cfg->weapons.shotgun; break;
		case WEAPON_RIFLE: b = &cfg->weapons.rifle; break;
		case WEAPON_SMG: b = &cfg->weapons.smg; break;
		case WEAPON_ROCKET: b = &cfg->weapons.rocket; break;
		default: b = NULL; break;
	}
	if (b) {
		tmp.ammo_per_shot = b->ammo_per_shot;
		tmp.shot_cooldown_s = b->shot_cooldown_s;
		tmp.pellets = b->pellets;
		tmp.spread_deg = b->spread_deg;
		tmp.proj_speed = b->proj_speed;
		tmp.proj_radius = b->proj_radius;
		tmp.proj_life_s = b->proj_life_s;
		tmp.proj_damage = b->proj_damage;
	}
	return &tmp;
}
