#include "assets/image.h"

#include "core/log.h"

#include <SDL.h>
#include <stdlib.h>
#include <string.h>

void image_destroy(Image* self) {
	free(self->pixels);
	memset(self, 0, sizeof(*self));
}

bool image_load_bmp(Image* out, const char* path) {
	memset(out, 0, sizeof(*out));
	SDL_Surface* src = SDL_LoadBMP(path);
	if (!src) {
		log_error("SDL_LoadBMP failed for %s: %s", path, SDL_GetError());
		return false;
	}
	SDL_Surface* surf = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ABGR8888, 0);
	SDL_FreeSurface(src);
	if (!surf) {
		log_error("SDL_ConvertSurfaceFormat failed: %s", SDL_GetError());
		return false;
	}

	out->width = surf->w;
	out->height = surf->h;
	size_t count = (size_t)out->width * (size_t)out->height;
	out->pixels = (uint32_t*)malloc(count * sizeof(uint32_t));
	if (!out->pixels) {
		SDL_FreeSurface(surf);
		return false;
	}
	memcpy(out->pixels, surf->pixels, count * sizeof(uint32_t));
	SDL_FreeSurface(surf);
	return true;
}
