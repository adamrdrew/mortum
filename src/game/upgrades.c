#include "game/upgrades.h"

#include "game/ammo.h"

static int imin(int a, int b) {
	return a < b ? a : b;
}

bool upgrades_apply_max_health(Player* player) {
	if (!player) {
		return false;
	}
	player->health_max += 25;
	player->health = imin(player->health_max, player->health + 25);
	return true;
}

bool upgrades_apply_max_ammo(Player* player) {
	if (!player) {
		return false;
	}
	ammo_increase_max(&player->ammo, AMMO_BULLETS, 30);
	ammo_increase_max(&player->ammo, AMMO_SHELLS, 10);
	ammo_increase_max(&player->ammo, AMMO_CELLS, 25);
	return true;
}
