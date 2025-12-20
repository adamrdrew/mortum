#include "game/corruption.h"

#include "game/mortum.h"
#include "game/tuning.h"

#include <math.h>
#include <string.h>

static float dist2(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

void corruption_update(Player* player, const EntityList* entities, double dt_s) {
	if (!player || !entities || dt_s <= 0.0) {
		return;
	}

	// Simple proximity hazards.
	const float hazard_r = 0.90f;
	const float hazard_r2 = hazard_r * hazard_r;

	bool in_hazard = false;
	for (int i = 0; i < entities->count; i++) {
		const Entity* e = &entities->items[i];
		if (!e->active) {
			continue;
		}
		if (strncmp(e->type, "hazard_", 7) != 0) {
			continue;
		}
		if (dist2(player->x, player->y, e->x, e->y) <= hazard_r2) {
			in_hazard = true;
			break;
		}
	}

	if (!in_hazard) {
		return;
	}

	int delta = (int)lroundf(MORTUM_HAZARD_RATE_PER_S * (float)dt_s);
	if (delta <= 0) {
		delta = 1;
	}
	mortum_add(player, delta);
}
