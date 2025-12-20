#include "game/projectiles.h"

#include "game/combat.h"
#include "game/collision.h"

#include <math.h>
#include <string.h>

static float dist2(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

void projectiles_update(Player* player, const World* world, EntityList* entities, double dt_s) {
	if (!entities || dt_s <= 0.0) {
		return;
	}

	const float player_r = 0.20f;

	for (int i = 0; i < entities->count; i++) {
		Entity* e = &entities->items[i];
		if (!e->active) {
			continue;
		}
		bool is_player_proj = (strcmp(e->type, "proj_player") == 0);
		bool is_enemy_proj = (strcmp(e->type, "proj_enemy") == 0);
		if (!is_player_proj && !is_enemy_proj) {
			continue;
		}

		e->lifetime_s -= (float)dt_s;
		if (e->lifetime_s <= 0.0f) {
			e->active = false;
			continue;
		}

		float to_x = e->x + e->vx * (float)dt_s;
		float to_y = e->y + e->vy * (float)dt_s;

		if (world) {
			CollisionMoveResult r = collision_move_circle(world, e->radius, e->x, e->y, to_x, to_y);
			e->x = r.out_x;
			e->y = r.out_y;
			if (r.collided) {
				e->active = false;
				continue;
			}
		} else {
			e->x = to_x;
			e->y = to_y;
		}

		// Hits
		if (is_enemy_proj && player) {
			float rr = e->radius + player_r;
			if (dist2(e->x, e->y, player->x, player->y) <= rr * rr) {
				combat_damage_player(player, e->damage);
				e->active = false;
				continue;
			}
		}

		if (is_player_proj) {
			for (int j = 0; j < entities->count; j++) {
				Entity* t = &entities->items[j];
				if (!t->active) {
					continue;
				}
				if (strncmp(t->type, "enemy_", 6) != 0) {
					continue;
				}

				float tr = (t->radius > 0.0f) ? t->radius : 0.20f;
				float rr = e->radius + tr;
				if (dist2(e->x, e->y, t->x, t->y) <= rr * rr) {
					combat_damage_entity(t, e->damage);
					e->active = false;
					break;
				}
			}
		}
	}
}
