#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"
#include "assets/menu.h"

// Loads a menu JSON file from Assets/Menus/<menu_file>.
// Returns false on any validation or IO error (and logs why).
// On entry, *out is overwritten with a fresh zeroed struct.
bool menu_load(MenuAsset* out, const AssetPaths* paths, const char* menu_file);
