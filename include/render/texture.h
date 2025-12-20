#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "assets/asset_paths.h"

typedef struct Texture {
	int width;
	int height;
	uint32_t* pixels; // owned ABGR8888 (matches framebuffer)
	char name[64];
} Texture;

typedef struct TextureRegistry {
	Texture* items; // owned
	int count;
	int capacity;
} TextureRegistry;

void texture_registry_init(TextureRegistry* self);
void texture_registry_destroy(TextureRegistry* self);

// Loads and caches a texture by filename under Assets/Images. Returns NULL on failure.
const Texture* texture_registry_get(TextureRegistry* self, const AssetPaths* paths, const char* filename);

// Nearest sampling, u/v in [0,1].
uint32_t texture_sample_nearest(const Texture* t, float u, float v);
