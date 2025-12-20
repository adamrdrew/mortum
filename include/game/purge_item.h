#pragma once

#include <stdbool.h>

#include "game/player.h"

// Consumes one purge item (if available) and reduces Mortum.
// Returns true if an item was consumed.
bool purge_item_use(Player* player);
