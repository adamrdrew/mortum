#include "platform/window.h"

#include "core/log.h"

#include <SDL.h>

bool window_create(Window* self, const char* title, int width, int height, bool vsync) {
	self->window = NULL;
	self->renderer = NULL;
	self->width = width;
	self->height = height;

	SDL_Window* w = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_SHOWN);
	if (!w) {
		log_error("SDL_CreateWindow failed: %s", SDL_GetError());
		return false;
	}

	Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
	if (vsync) {
		renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
	}
	SDL_Renderer* r = SDL_CreateRenderer(w, -1, renderer_flags);
	if (!r) {
		// Fall back to software renderer (still ok; our renderer is CPU anyway).
		r = SDL_CreateRenderer(w, -1, SDL_RENDERER_SOFTWARE);
	}
	if (!r) {
		log_error("SDL_CreateRenderer failed: %s", SDL_GetError());
		SDL_DestroyWindow(w);
		return false;
	}

	self->window = w;
	self->renderer = r;
	return true;
}

void window_destroy(Window* self) {
	if (self->renderer) {
		SDL_DestroyRenderer(self->renderer);
		self->renderer = NULL;
	}
	if (self->window) {
		SDL_DestroyWindow(self->window);
		self->window = NULL;
	}
}
