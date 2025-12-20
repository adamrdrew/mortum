#include "game/mortum.h"

static int clampi(int v, int lo, int hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

void mortum_add(Player* player, int delta) {
	if (!player || delta == 0) {
		return;
	}
	player->mortum_pct = clampi(player->mortum_pct + delta, 0, 100);
}

void mortum_set(Player* player, int value) {
	if (!player) {
		return;
	}
	player->mortum_pct = clampi(value, 0, 100);
}
