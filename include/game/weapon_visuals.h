#pragma once

#include "game/weapon_defs.h"

// Static mapping from WeaponId to asset directory + filename prefix.
// Assets live under: Assets/Images/Weapons/<dir>/<prefix>-*.png
//
// Note: This intentionally returns small string views (const char*) so callers
// can build paths without allocations.

typedef struct WeaponVisualSpec {
	const char* dir_name; // e.g., "Handgun"
	const char* prefix; // e.g., "HANDGUN"
} WeaponVisualSpec;

// Returns NULL if id is out of range.
const WeaponVisualSpec* weapon_visual_spec_get(WeaponId id);

// Convenience: returns a short lowercase token for entity strings and UI.
// Examples: "handgun", "shotgun", "rifle", "smg", "rocket".
const char* weapon_visual_token(WeaponId id);
