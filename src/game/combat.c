#include "game/combat.h"

static int imax(int a, int b) {
	return a > b ? a : b;
}

void combat_damage_player(Player* player, int damage) {
	if (!player || damage <= 0) {
		return;
	}
	player->health = imax(0, player->health - damage);
}

void combat_damage_entity(Entity* entity, int damage) {
	if (!entity || !entity->active || damage <= 0) {
		return;
	}
	entity->health -= damage;
	if (entity->health <= 0) {
		entity->active = false;
		entity->just_died = true;
		entity->health = 0;
	}
}
