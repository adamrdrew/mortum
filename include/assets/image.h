#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Image {
	int width;
	int height;
	uint32_t* pixels; // owned RGBA8888
} Image;

void image_destroy(Image* self);

bool image_load_bmp(Image* out, const char* path);
