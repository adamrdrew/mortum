#include "game/enemy.h"

#include "game/combat.h"
#include "game/collision.h"

#include <math.h>
#include <string.h>

static float clamp_min0(float v) {
	return v < 0.0f ? 0.0f : v;
}

static float dist2(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

void enemy_update(Player* player, const World* world, EntityList* entities, double dt_s) {
	if (!player || !entities || dt_s <= 0.0) {
		return;
	}

	const float player_r = 0.20f;

	for (int i = 0; i < entities->count; i++) {
		Entity* e = &entities->items[i];
		if (!e->active) {
			continue;
		}
		if (strncmp(e->type, "enemy_", 6) != 0) {
			continue;
		}

		// Tick timers
		e->cooldown_s = clamp_min0(e->cooldown_s - (float)dt_s);

		bool is_melee = (strcmp(e->type, "enemy_melee") == 0);
		bool is_turret = (strcmp(e->type, "enemy_turret") == 0);
		if (!is_melee && !is_turret) {
			continue;
		}

		// Defaults
		if (e->radius <= 0.0f) {
			e->radius = is_melee ? 0.18f : 0.22f;
		}
		if (e->health <= 0) {
			e->health = is_melee ? 60 : 80;
		}

		if (is_melee) {
			// Move toward player
			float dx = player->x - e->x;
			float dy = player->y - e->y;
			float d2 = dx * dx + dy * dy;
			float d = (d2 > 1e-8f) ? sqrtf(d2) : 0.0f;
			if (d > 1e-4f) {
				dx /= d;
				dy /= d;
			}

			const float speed = 1.10f;
			float to_x = e->x + dx * speed * (float)dt_s;
			float to_y = e->y + dy * speed * (float)dt_s;
			if (world) {
				CollisionMoveResult r = collision_move_circle(world, e->radius, e->x, e->y, to_x, to_y);
				e->x = r.out_x;
				e->y = r.out_y;
			} else {
				e->x = to_x;
				e->y = to_y;
			}

			// Melee hit
			float rr = e->radius + player_r;
			if (dist2(e->x, e->y, player->x, player->y) <= rr * rr) {
				if (e->cooldown_s <= 0.0f) {
					combat_damage_player(player, 10);
					e->cooldown_s = 0.85f;
				}
			}
		} else if (is_turret) {
			// Fire a projectile toward the player
			if (e->cooldown_s <= 0.0f) {
				float dx = player->x - e->x;
				float dy = player->y - e->y;
				float len = sqrtf(dx * dx + dy * dy);
				if (len > 1e-6f) {
					dx /= len;
					dy /= len;
				}

				Entity proj;
				entity_init(&proj);
				strncpy(proj.type, "proj_enemy", sizeof(proj.type) - 1);
				proj.type[sizeof(proj.type) - 1] = '\0';
				proj.x = e->x + dx * 0.10f;
				proj.y = e->y + dy * 0.10f;
				proj.radius = 0.06f;
				proj.vx = dx * 3.8f;
				proj.vy = dy * 3.8f;
				proj.lifetime_s = 2.5f;
				proj.damage = 8;

				(void)entity_list_push(entities, &proj);
				e->cooldown_s = 1.10f;
			}
		}
	}
}
