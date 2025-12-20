#pragma once

#include <stdbool.h>

typedef struct AssetPaths {
	char* assets_root; // owned
} AssetPaths;

bool asset_paths_init(AssetPaths* self, const char* base_path);
void asset_paths_destroy(AssetPaths* self);

// Returns an owned path to a file under Assets/ (caller frees).
char* asset_path_join(const AssetPaths* self, const char* subdir, const char* filename);
