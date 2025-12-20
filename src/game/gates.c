#include "game/gates.h"

#include <math.h>
#include <string.h>

static float dist2(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

void gates_update_and_resolve(Player* player, EntityList* entities) {
	if (!player || !entities) {
		return;
	}

	const float gate_r = 0.35f;
	const float gate_r2 = gate_r * gate_r;
	for (int i = 0; i < entities->count; i++) {
		Entity* e = &entities->items[i];
		if (!e->active) {
			continue;
		}
		if (strcmp(e->type, "gate_key") != 0) {
			continue;
		}

		float d2 = dist2(player->x, player->y, e->x, e->y);
		if ((player->keys & 1) != 0) {
			// Gate opens (deactivates) once the player reaches it with the key.
			if (d2 <= gate_r2) {
				e->active = false;
			}
			continue;
		}

		if (d2 >= gate_r2 || d2 < 1e-8f) {
			continue;
		}

		float d = sqrtf(d2);
		float nx = (player->x - e->x) / d;
		float ny = (player->y - e->y) / d;
		float push = gate_r - d;
		player->x += nx * push;
		player->y += ny * push;
	}
}
