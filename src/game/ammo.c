#include "game/ammo.h"

#include <stddef.h>

static int clampi(int v, int lo, int hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

const char* ammo_type_name(AmmoType t) {
	switch (t) {
		case AMMO_BULLETS:
			return "Bullets";
		case AMMO_SHELLS:
			return "Shells";
		case AMMO_CELLS:
			return "Cells";
		default:
			return "Unknown";
	}
}

void ammo_state_init(AmmoState* a) {
	if (!a) {
		return;
	}
	for (int i = 0; i < (int)AMMO_TYPE_COUNT; i++) {
		a->cur[i] = 0;
		a->max[i] = 0;
	}
}

bool ammo_add(AmmoState* a, AmmoType t, int amount) {
	if (!a || amount <= 0 || (int)t < 0 || t >= AMMO_TYPE_COUNT) {
		return false;
	}
	int i = (int)t;
	int before = a->cur[i];
	a->cur[i] = clampi(a->cur[i] + amount, 0, a->max[i]);
	return a->cur[i] != before;
}

bool ammo_consume(AmmoState* a, AmmoType t, int amount) {
	if (!a || amount <= 0 || (int)t < 0 || t >= AMMO_TYPE_COUNT) {
		return false;
	}
	int i = (int)t;
	if (a->cur[i] < amount) {
		return false;
	}
	a->cur[i] -= amount;
	return true;
}

int ammo_get(const AmmoState* a, AmmoType t) {
	if (!a || (int)t < 0 || t >= AMMO_TYPE_COUNT) {
		return 0;
	}
	return a->cur[(int)t];
}

int ammo_get_max(const AmmoState* a, AmmoType t) {
	if (!a || (int)t < 0 || t >= AMMO_TYPE_COUNT) {
		return 0;
	}
	return a->max[(int)t];
}

void ammo_increase_max(AmmoState* a, AmmoType t, int amount) {
	if (!a || amount <= 0 || (int)t < 0 || t >= AMMO_TYPE_COUNT) {
		return;
	}
	int i = (int)t;
	a->max[i] += amount;
	if (a->max[i] < 0) {
		a->max[i] = 0;
	}
	if (a->cur[i] > a->max[i]) {
		a->cur[i] = a->max[i];
	}
}
