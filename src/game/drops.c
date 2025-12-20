#include "game/drops.h"

#include <string.h>

void drops_update(Player* player, EntityList* entities) {
	if (!player || !entities) {
		return;
	}
	if (!player->undead_active) {
		// Clear any stale just_died markers.
		for (int i = 0; i < entities->count; i++) {
			entities->items[i].just_died = false;
		}
		return;
	}

	for (int i = 0; i < entities->count; i++) {
		Entity* e = &entities->items[i];
		if (!e->just_died) {
			continue;
		}
		e->just_died = false;

		if (strncmp(e->type, "enemy_", 6) != 0) {
			continue;
		}

		Entity shard;
		entity_init(&shard);
		strncpy(shard.type, "pickup_shard", sizeof(shard.type) - 1);
		shard.type[sizeof(shard.type) - 1] = '\0';
		shard.x = e->x;
		shard.y = e->y;
		shard.z = 0.0f;
		shard.radius = 0.18f;
		(void)entity_list_push(entities, &shard);
	}
}
