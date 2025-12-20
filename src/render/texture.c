#include "render/texture.h"

#include "assets/image.h"

#include <stdlib.h>
#include <string.h>

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
	if (!self || !paths || !filename || filename[0] == '\0') {
		return NULL;
	}

	Texture* existing = registry_find(self, filename);
	if (existing) {
		return existing;
	}

	char* full = asset_path_join(paths, "Images", filename);
	if (!full) {
		return NULL;
	}

	Image img;
	bool ok = image_load_bmp(&img, full);
	free(full);
	if (!ok) {
		return NULL;
	}

	Texture* t = registry_push(self);
	if (!t) {
		image_destroy(&img);
		return NULL;
	}

	t->width = img.width;
	t->height = img.height;
	t->pixels = img.pixels;
	img.pixels = NULL;
	strncpy(t->name, filename, sizeof(t->name) - 1);

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
