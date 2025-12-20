#pragma once

#include <stdbool.h>

typedef enum AmmoType {
	AMMO_BULLETS = 0,
	AMMO_SHELLS = 1,
	AMMO_CELLS = 2,
	AMMO_TYPE_COUNT
} AmmoType;

typedef struct AmmoState {
	int cur[AMMO_TYPE_COUNT];
	int max[AMMO_TYPE_COUNT];
} AmmoState;

const char* ammo_type_name(AmmoType t);

void ammo_state_init(AmmoState* a);

bool ammo_add(AmmoState* a, AmmoType t, int amount);
bool ammo_consume(AmmoState* a, AmmoType t, int amount);
int ammo_get(const AmmoState* a, AmmoType t);
int ammo_get_max(const AmmoState* a, AmmoType t);
void ammo_increase_max(AmmoState* a, AmmoType t, int amount);
