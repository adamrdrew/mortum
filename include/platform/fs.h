#pragma once

#include <stdbool.h>

typedef struct FsPaths {
	char* base_path;   // owned; free with fs_paths_destroy
	char* pref_path;   // owned; free with fs_paths_destroy
} FsPaths;

bool fs_paths_init(FsPaths* self, const char* org, const char* app);
void fs_paths_destroy(FsPaths* self);
