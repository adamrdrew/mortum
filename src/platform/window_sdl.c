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

bool window_is_fullscreen(const Window* self) {
	if (!self || !self->window) {
		return false;
	}
	Uint32 f = SDL_GetWindowFlags(self->window);
	return (f & SDL_WINDOW_FULLSCREEN) != 0 || (f & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

bool window_set_fullscreen(Window* self, bool fullscreen) {
	if (!self || !self->window) {
		return false;
	}
	Uint32 desired = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
	if (SDL_SetWindowFullscreen(self->window, desired) != 0) {
		log_error("SDL_SetWindowFullscreen failed: %s", SDL_GetError());
		return false;
	}
	SDL_GetWindowSize(self->window, &self->width, &self->height);
	return true;
}

void window_set_size(Window* self, int width, int height) {
	if (!self || !self->window) {
		return;
	}
	if (width <= 0 || height <= 0) {
		return;
	}
	SDL_SetWindowSize(self->window, width, height);
	SDL_GetWindowSize(self->window, &self->width, &self->height);
}

void window_center(Window* self) {
	if (!self || !self->window) {
		return;
	}
	SDL_SetWindowPosition(self->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}
