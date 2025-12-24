#include "render/texture.h"

#include "assets/image.h"

#include "core/log.h"

#include "platform/time.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TextureRegistryPerf* g_perf = NULL;

void texture_registry_perf_begin(TextureRegistryPerf* perf) {
	g_perf = perf;
	if (g_perf) {
		g_perf->get_calls = 0;
		g_perf->registry_string_compares = 0;
		g_perf->get_ms = 0.0;
	}
}

void texture_registry_perf_end(void) {
	g_perf = NULL;
}

static bool file_exists(const char* path) {
	if (!path || path[0] == '\0') {
		return false;
	}
	FILE* f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	fclose(f);
	return true;
}

static int clampi(int v, int lo, int hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static float clampf(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

void texture_registry_init(TextureRegistry* self) {
	memset(self, 0, sizeof(*self));
}

void texture_registry_destroy(TextureRegistry* self) {
	if (!self) {
		return;
	}
	for (int i = 0; i < self->count; i++) {
		free(self->items[i].pixels);
		self->items[i].pixels = NULL;
	}
	free(self->items);
	memset(self, 0, sizeof(*self));
}

static Texture* registry_find(TextureRegistry* self, const char* filename) {
	for (int i = 0; i < self->count; i++) {
		if (g_perf) {
			g_perf->registry_string_compares++;
		}
		if (strncmp(self->items[i].name, filename, sizeof(self->items[i].name)) == 0) {
			return &self->items[i];
		}
	}
	return NULL;
}

static Texture* registry_push(TextureRegistry* self) {
	if (self->count >= self->capacity) {
		int new_cap = self->capacity == 0 ? 8 : self->capacity * 2;
		Texture* n = (Texture*)realloc(self->items, (size_t)new_cap * sizeof(Texture));
		if (!n) {
			return NULL;
		}
		self->items = n;
		self->capacity = new_cap;
	}
	Texture* t = &self->items[self->count++];
	memset(t, 0, sizeof(*t));
	return t;
}

const Texture* texture_registry_get(TextureRegistry* self, const AssetPaths* paths, const char* filename) {
	double t0 = 0.0;
	if (g_perf) {
		g_perf->get_calls++;
		t0 = platform_time_seconds();
	}
	if (!self || !paths || !filename || filename[0] == '\0') {
		if (g_perf) {
			g_perf->get_ms += (platform_time_seconds() - t0) * 1000.0;
		}
		return NULL;
	}

	Texture* existing = registry_find(self, filename);
	if (existing) {
		// Cache negative lookups as entries with NULL pixels.
		if (g_perf) {
			g_perf->get_ms += (platform_time_seconds() - t0) * 1000.0;
		}
		return existing->pixels ? existing : NULL;
	}

	Image img;
	bool ok = false;
	bool enforce_64 = false;

	char* preferred = asset_path_join(paths, "Images/Textures", filename);
	char* sprites = asset_path_join(paths, "Images/Sprites", filename);
	char* sky = asset_path_join(paths, "Images/Sky", filename);
	char* fallback = asset_path_join(paths, "Images", filename);
	const char* preferred_s = preferred ? preferred : "(alloc failed)";
	const char* sprites_s = sprites ? sprites : "(alloc failed)";
	const char* sky_s = sky ? sky : "(alloc failed)";
	const char* fallback_s = fallback ? fallback : "(alloc failed)";

	// Prefer the new texture library directory (64x64 enforced here only).
	if (preferred && file_exists(preferred)) {
		ok = image_load_auto(&img, preferred);
		enforce_64 = true;
	}
	if (ok && enforce_64 && (img.width != 64 || img.height != 64)) {
		log_error("Texture %s must be 64x64, got %dx%d", filename, img.width, img.height);
		image_destroy(&img);
		ok = false;
	}

	// Sprite directory (no size enforcement).
	if (!ok && sprites && file_exists(sprites)) {
		ok = image_load_auto(&img, sprites);
	}

	// Skybox directory (no size enforcement).
	if (!ok && sky && file_exists(sky)) {
		ok = image_load_auto(&img, sky);
	}

	// Backward-compatible fallback.
	if (!ok && fallback && file_exists(fallback)) {
		ok = image_load_auto(&img, fallback);
	}

	if (!ok) {
		log_error("Failed to load texture %s (tried %s; sprites %s; sky %s; fallback %s)", filename, preferred_s, sprites_s, sky_s, fallback_s);
		// Cache miss to avoid repeated disk I/O and log spam every frame.
		Texture* miss = registry_push(self);
		if (miss) {
			strncpy(miss->name, filename, sizeof(miss->name) - 1);
		}
		free(preferred);
		free(sprites);
		free(sky);
		free(fallback);
		if (g_perf) {
			g_perf->get_ms += (platform_time_seconds() - t0) * 1000.0;
		}
		return NULL;
	}

	free(preferred);
	free(sprites);
	free(sky);
	free(fallback);

	Texture* t = registry_push(self);
	if (!t) {
		image_destroy(&img);
		if (g_perf) {
			g_perf->get_ms += (platform_time_seconds() - t0) * 1000.0;
		}
		return NULL;
	}

	t->width = img.width;
	t->height = img.height;
	t->pixels = img.pixels;
	img.pixels = NULL;
	strncpy(t->name, filename, sizeof(t->name) - 1);
	if (g_perf) {
		g_perf->get_ms += (platform_time_seconds() - t0) * 1000.0;
	}
	return t;
}

uint32_t texture_sample_nearest(const Texture* t, float u, float v) {
	if (!t || !t->pixels || t->width <= 0 || t->height <= 0) {
		return 0xFFFF00FFu;
	}
	u = clampf(u, 0.0f, 1.0f);
	v = clampf(v, 0.0f, 1.0f);
	int x = (int)(u * (float)(t->width - 1) + 0.5f);
	int y = (int)(v * (float)(t->height - 1) + 0.5f);
	x = clampi(x, 0, t->width - 1);
	y = clampi(y, 0, t->height - 1);
	return t->pixels[y * t->width + x];
}
