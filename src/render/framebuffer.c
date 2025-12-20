#include "render/framebuffer.h"

#include <stdlib.h>
#include <string.h>

bool framebuffer_init(Framebuffer* self, int width, int height) {
	self->width = width;
	self->height = height;
	self->pixels = NULL;

	size_t count = (size_t)width * (size_t)height;
	self->pixels = (uint32_t*)malloc(count * sizeof(uint32_t));
	if (!self->pixels) {
		return false;
	}
	memset(self->pixels, 0, count * sizeof(uint32_t));
	return true;
}

void framebuffer_destroy(Framebuffer* self) {
	free(self->pixels);
	self->pixels = NULL;
	self->width = 0;
	self->height = 0;
}
