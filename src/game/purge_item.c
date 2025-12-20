#include "game/purge_item.h"

#include "game/mortum.h"
#include "game/tuning.h"

#include <stdbool.h>

bool purge_item_use(Player* player) {
	if (!player) {
		return false;
	}
	if (player->purge_items <= 0) {
		return false;
	}

	player->purge_items -= 1;
	mortum_add(player, -PURGE_ITEM_MORTUM_REDUCE);
	return true;
}
