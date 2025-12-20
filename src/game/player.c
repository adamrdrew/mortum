#include "game/player.h"

#include "game/ammo.h"
#include "game/weapon_defs.h"

#include <string.h>

void player_init(Player* p) {
	memset(p, 0, sizeof(*p));
	p->health_max = 100;
	p->health = 100;

	ammo_state_init(&p->ammo);
	// Baseline capacities; upgraded via pickup_upgrade_max_ammo.
	p->ammo.max[AMMO_BULLETS] = 120;
	p->ammo.max[AMMO_SHELLS] = 40;
	p->ammo.max[AMMO_CELLS] = 100;
	// Starting ammo.
	p->ammo.cur[AMMO_BULLETS] = 30;
	p->ammo.cur[AMMO_SHELLS] = 0;
	p->ammo.cur[AMMO_CELLS] = 0;

	p->weapons_owned_mask = (1u << (unsigned)WEAPON_SIDEARM);
	p->weapon_equipped = WEAPON_SIDEARM;
	p->mortum_pct = 0;
	p->keys = 0;
	p->purge_items = 0;
	p->weapon_cooldown_s = 0.0f;
	p->noclip = false;
	p->noclip_prev_down = false;
}
