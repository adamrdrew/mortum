#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Image {
	int width;
	int height;
	uint32_t* pixels; // owned ABGR8888 (matches framebuffer)
} Image;

void image_destroy(Image* self);

bool image_load_bmp(Image* out, const char* path);

// Loads a PNG and converts to ABGR8888.
bool image_load_png(Image* out, const char* path);

// Loads an image based on file extension (.png/.bmp).
bool image_load_auto(Image* out, const char* path);
