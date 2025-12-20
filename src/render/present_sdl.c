#include "render/present.h"

#include "core/log.h"

#include <SDL.h>

bool present_init(Presenter* self, Window* window, const Framebuffer* fb) {
	self->texture = NULL;
	SDL_Texture* tex = SDL_CreateTexture(window->renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, fb->width, fb->height);
	if (!tex) {
		log_error("SDL_CreateTexture failed: %s", SDL_GetError());
		return false;
	}
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_NONE);
	self->texture = tex;
	return true;
}

void present_shutdown(Presenter* self) {
	if (self->texture) {
		SDL_DestroyTexture((SDL_Texture*)self->texture);
		self->texture = NULL;
	}
}

bool present_frame(Presenter* self, Window* window, const Framebuffer* fb) {
	SDL_Texture* tex = (SDL_Texture*)self->texture;
	if (!tex) {
		return false;
	}

	if (SDL_UpdateTexture(tex, NULL, fb->pixels, fb->width * (int)sizeof(uint32_t)) != 0) {
		log_error("SDL_UpdateTexture failed: %s", SDL_GetError());
		return false;
	}

	SDL_RenderClear(window->renderer);
	SDL_RenderCopy(window->renderer, tex, NULL, NULL);
	SDL_RenderPresent(window->renderer);
	return true;
}
