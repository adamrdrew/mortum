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
	Texture** items; // owned pointers; each Texture is heap-allocated and stable
	int count;
	int capacity;
} TextureRegistry;

// Optional perf counters for profiling. Only active when explicitly enabled.
typedef struct TextureRegistryPerf {
	uint32_t get_calls;
	uint32_t registry_string_compares;
	double get_ms;
} TextureRegistryPerf;

// Enable perf capture for texture lookups. Passing NULL disables.
void texture_registry_perf_begin(TextureRegistryPerf* perf);
void texture_registry_perf_end(void);

void texture_registry_init(TextureRegistry* self);
void texture_registry_destroy(TextureRegistry* self);

// Loads and caches a texture by filename.
// Preferred location: Assets/Images/Textures/<filename>
// Backward-compatible fallback: Assets/Images/<filename>
// Supported formats: .PNG/.png and .bmp (via assets/image loaders).
// Note: current map textures are expected to be 64x64 when using PNG.
// Returns NULL on failure.
const Texture* texture_registry_get(TextureRegistry* self, const AssetPaths* paths, const char* filename);

// Nearest sampling, u/v in [0,1].
uint32_t texture_sample_nearest(const Texture* t, float u, float v);
