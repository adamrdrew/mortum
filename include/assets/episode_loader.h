#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"

typedef struct Episode {
	char* name;   // owned
	char* splash; // owned
	char** enter_scenes; // owned array of owned strings
	int enter_scene_count;
	char** exit_scenes; // owned array of owned strings
	int exit_scene_count;
	char** maps;  // owned array of owned strings
	int map_count;
} Episode;

void episode_destroy(Episode* self);

bool episode_load(Episode* out, const AssetPaths* paths, const char* episode_filename);
