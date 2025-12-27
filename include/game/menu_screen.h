#pragma once

#include <stdbool.h>

#include "assets/menu.h"
#include "game/screen.h"

// Forward declaration to avoid including the whole console command header.
typedef struct ConsoleCommandContext ConsoleCommandContext;

// Creates a Menu-backed screen. Takes ownership of `asset` (moves it).
// invoked_from_timeline controls Escape behavior at the root menu.
// cmd_ctx is used to queue deferred console commands safely.
Screen* menu_screen_create(MenuAsset asset, bool invoked_from_timeline, ConsoleCommandContext* cmd_ctx);
