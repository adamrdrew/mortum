#include "game/exit.h"

#include <math.h>
#include <string.h>

static float dist2(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

bool exit_check_reached(const Player* player, const EntityList* entities) {
	if (!player || !entities) {
		return false;
	}
	const float r2 = 0.5f * 0.5f;
	for (int i = 0; i < entities->count; i++) {
		const Entity* e = &entities->items[i];
		if (!e->active) {
			continue;
		}
		if (strcmp(e->type, "exit") != 0) {
			continue;
		}
		if (dist2(player->x, player->y, e->x, e->y) <= r2) {
			return true;
		}
	}
	return false;
}
