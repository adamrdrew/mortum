#include "game/weapon_visuals.h"

#include <stddef.h>

static const WeaponVisualSpec k_weapon_visuals[WEAPON_COUNT] = {
	[WEAPON_HANDGUN] = {
		.dir_name = "Handgun",
		.prefix = "HANDGUN",
	},
	[WEAPON_SHOTGUN] = {
		.dir_name = "Shotgun",
		.prefix = "SHOTGUN",
	},
	[WEAPON_RIFLE] = {
		.dir_name = "Rifle",
		.prefix = "RIFLE",
	},
	[WEAPON_SMG] = {
		.dir_name = "SMG",
		.prefix = "SMG",
	},
	[WEAPON_ROCKET] = {
		.dir_name = "Rocket",
		.prefix = "ROCKET",
	},
};

const WeaponVisualSpec* weapon_visual_spec_get(WeaponId id) {
	if ((int)id < 0 || id >= WEAPON_COUNT) {
		return NULL;
	}
	return &k_weapon_visuals[(int)id];
}

const char* weapon_visual_token(WeaponId id) {
	switch (id) {
		case WEAPON_HANDGUN:
			return "handgun";
		case WEAPON_SHOTGUN:
			return "shotgun";
		case WEAPON_RIFLE:
			return "rifle";
		case WEAPON_SMG:
			return "smg";
		case WEAPON_ROCKET:
			return "rocket";
		default:
			return "unknown";
	}
}
