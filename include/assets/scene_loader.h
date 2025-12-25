#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"
#include "assets/scene.h"

// Loads a scene JSON file from Assets/Scenes/<scene_file>.
// Returns false on any validation or IO error (and logs why).
bool scene_load(Scene* out, const AssetPaths* paths, const char* scene_file);
