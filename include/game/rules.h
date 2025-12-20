#pragma once

#include "game/player.h"

// Edge-case ordering rules.
//
// Returns true if Undead mode should be allowed to trigger this tick.
// If the player is already dead (health<=0), it returns false.
bool rules_allow_undead_trigger(const Player* player);
