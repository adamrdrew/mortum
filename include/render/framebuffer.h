#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Framebuffer {
	int width;
	int height;
	uint32_t* pixels; // RGBA8888
} Framebuffer;

bool framebuffer_init(Framebuffer* self, int width, int height);
void framebuffer_destroy(Framebuffer* self);

static inline uint32_t* framebuffer_pixel(Framebuffer* self, int x, int y) {
	return &self->pixels[y * self->width + x];
}
