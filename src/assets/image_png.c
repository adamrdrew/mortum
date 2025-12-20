#include "assets/image.h"

#include "core/log.h"

#include <SDL.h>

#include "lodepng.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static bool ends_with_ci(const char* s, const char* suffix) {
	if (!s || !suffix) {
		return false;
	}
	size_t ns = strlen(s);
	size_t nf = strlen(suffix);
	if (nf > ns) {
		return false;
	}
	const char* tail = s + (ns - nf);
	for (size_t i = 0; i < nf; i++) {
		char a = (char)tolower((unsigned char)tail[i]);
		char b = (char)tolower((unsigned char)suffix[i]);
		if (a != b) {
			return false;
		}
	}
	return true;
}

bool image_load_png(Image* out, const char* path) {
	memset(out, 0, sizeof(*out));
	if (!path || path[0] == '\0') {
		return false;
	}

	unsigned char* rgba = NULL;
	unsigned w = 0;
	unsigned h = 0;
	unsigned err = lodepng_decode32_file(&rgba, &w, &h, path);
	if (err != 0 || !rgba || w == 0 || h == 0) {
		log_error("lodepng_decode32_file failed for %s: %u (%s)", path, err, lodepng_error_text(err));
		free(rgba);
		return false;
	}

	SDL_Surface* src = SDL_CreateRGBSurfaceWithFormatFrom(
		rgba,
		(int)w,
		(int)h,
		32,
		(int)(w * 4u),
		SDL_PIXELFORMAT_RGBA32);
	if (!src) {
		log_error("SDL_CreateRGBSurfaceWithFormatFrom failed for %s: %s", path, SDL_GetError());
		free(rgba);
		return false;
	}

	SDL_Surface* surf = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ABGR8888, 0);
	SDL_FreeSurface(src);
	free(rgba);
	if (!surf) {
		log_error("SDL_ConvertSurfaceFormat failed for %s: %s", path, SDL_GetError());
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

bool image_load_auto(Image* out, const char* path) {
	if (ends_with_ci(path, ".png")) {
		return image_load_png(out, path);
	}
	if (ends_with_ci(path, ".bmp")) {
		return image_load_bmp(out, path);
	}
	// Unknown extension: try PNG first, then BMP.
	if (image_load_png(out, path)) {
		return true;
	}
	return image_load_bmp(out, path);
}
