#include "game/undead_mode.h"

#include "game/combat.h"
#include "game/drops.h"
#include "game/mortum.h"
#include "game/tuning.h"

#include <math.h>

void undead_mode_update(Player* player, EntityList* entities, double dt_s) {
	if (!player || dt_s <= 0.0) {
		return;
	}

	// Start Undead mode when Mortum reaches 100.
	if (!player->undead_active && player->mortum_pct >= 100) {
		player->undead_active = true;
		player->undead_shards_required = UNDEAD_SHARDS_REQUIRED;
		player->undead_shards_collected = 0;
		mortum_set(player, 100);
	}

	if (player->undead_active) {
		// Drain health over time.
		int dmg = (int)lroundf(UNDEAD_HEALTH_DRAIN_PER_S * (float)dt_s);
		if (dmg <= 0) {
			dmg = 1;
		}
		combat_damage_player(player, dmg);

		// Spawn shard pickups from enemies killed during undead.
		if (entities) {
			drops_update(player, entities);
		}

		// Recover once enough shards collected.
		if (player->undead_shards_collected >= player->undead_shards_required) {
			player->undead_active = false;
			player->undead_shards_required = 0;
			player->undead_shards_collected = 0;
			mortum_set(player, 25);
		}
	}
}
