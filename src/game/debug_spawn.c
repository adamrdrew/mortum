#include "game/debug_spawn.h"

#include <math.h>
#include <string.h>

void debug_spawn_enemy(Player* player, const World* world, EntityList* entities) {
	(void)world;
	if (!player || !entities) {
		return;
	}

	float ang = player->angle_deg * (float)M_PI / 180.0f;
	float dx = cosf(ang);
	float dy = sinf(ang);

	Entity e;
	entity_init(&e);
	strncpy(e.type, "enemy_melee", sizeof(e.type) - 1);
	e.type[sizeof(e.type) - 1] = '\0';
	e.x = player->x + dx * 1.5f;
	e.y = player->y + dy * 1.5f;
	e.radius = 0.25f;
	e.health = 60;
	(void)entity_list_push(entities, &e);
}
