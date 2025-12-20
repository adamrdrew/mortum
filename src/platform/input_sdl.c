#include "platform/input.h"

#include <SDL.h>
#include <string.h>

void input_begin_frame(Input* self) {
	self->quit_requested = false;
	self->mouse_dx = 0;
	self->mouse_dy = 0;
	self->mouse_wheel = 0;
	// keep keys_down and mouse_buttons persistent
}

void input_poll(Input* self) {
	SDL_Event ev;
	while (SDL_PollEvent(&ev) != 0) {
		switch (ev.type) {
			case SDL_QUIT:
				self->quit_requested = true;
				break;
			case SDL_KEYDOWN:
			case SDL_KEYUP: {
				int sc = ev.key.keysym.scancode;
				if (sc >= 0 && sc < (int)(sizeof(self->keys_down) / sizeof(self->keys_down[0]))) {
					self->keys_down[sc] = (ev.type == SDL_KEYDOWN);
				}
			} break;
			case SDL_MOUSEMOTION:
				self->mouse_dx += ev.motion.xrel;
				self->mouse_dy += ev.motion.yrel;
				break;
			case SDL_MOUSEWHEEL:
				self->mouse_wheel += ev.wheel.y;
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP: {
				uint32_t mask = SDL_BUTTON(ev.button.button);
				if (ev.type == SDL_MOUSEBUTTONDOWN) {
					self->mouse_buttons |= mask;
				} else {
					self->mouse_buttons &= ~mask;
				}
			} break;
			default:
				break;
		}
	}
}
