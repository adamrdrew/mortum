#include "game/rules.h"

bool rules_allow_undead_trigger(const Player* player) {
	if (!player) {
		return false;
	}
	// Simple policy: fatal damage wins over undead trigger.
	return player->health > 0;
}
