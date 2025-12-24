#include "platform/input.h"

#include <SDL.h>
#include <string.h>

void input_begin_frame(Input* self) {
	self->quit_requested = false;
	self->key_event_count = 0;
	self->text_utf8_len = 0;
	self->mouse_dx = 0;
	self->mouse_dy = 0;
	self->mouse_wheel = 0;
	// keep keys_down and mouse_buttons persistent
}

void input_poll(Input* self) {
	static bool text_input_started = false;
	if (!text_input_started) {
		SDL_StartTextInput();
		text_input_started = true;
	}

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
				if (ev.type == SDL_KEYDOWN) {
					if (self->key_event_count < INPUT_KEY_EVENTS_MAX) {
						self->key_events[self->key_event_count].scancode = sc;
						self->key_events[self->key_event_count].repeat = (ev.key.repeat != 0);
						self->key_event_count++;
					}
				}
			} break;
			case SDL_TEXTINPUT: {
				// SDL guarantees this is a null-terminated UTF-8 string.
				const char* s = ev.text.text;
				size_t n = s ? strlen(s) : 0u;
				int cap = (int)sizeof(self->text_utf8);
				int avail = cap - self->text_utf8_len;
				if (avail > 0 && n > 0u) {
					int to_copy = (int)n;
					if (to_copy > avail) {
						to_copy = avail;
					}
					memcpy(self->text_utf8 + self->text_utf8_len, s, (size_t)to_copy);
					self->text_utf8_len += to_copy;
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
