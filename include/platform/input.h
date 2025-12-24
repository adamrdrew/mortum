#pragma once

#include <stdbool.h>
#include <stdint.h>

#define INPUT_TEXT_UTF8_MAX 128
#define INPUT_KEY_EVENTS_MAX 64

typedef struct InputKeyEvent {
	int scancode;
	bool repeat;
} InputKeyEvent;

typedef struct Input {
	bool quit_requested;
	bool keys_down[512];
	// Discrete key events captured this frame (key down only).
	int key_event_count;
	InputKeyEvent key_events[INPUT_KEY_EVENTS_MAX];

	// UTF-8 text typed this frame (SDL_TEXTINPUT). Not null-terminated.
	int text_utf8_len;
	char text_utf8[INPUT_TEXT_UTF8_MAX];

	int mouse_dx;
	int mouse_dy;
	int mouse_wheel;
	uint32_t mouse_buttons;
} Input;

void input_begin_frame(Input* self);
void input_poll(Input* self);

static inline bool input_key_down(const Input* self, int scancode) {
	if (scancode < 0 || scancode >= (int)(sizeof(self->keys_down) / sizeof(self->keys_down[0]))) {
		return false;
	}
	return self->keys_down[scancode];
}

static inline bool input_key_pressed(const Input* self, int scancode) {
	if (!self) {
		return false;
	}
	for (int i = 0; i < self->key_event_count; i++) {
		if (self->key_events[i].scancode == scancode) {
			return true;
		}
	}
	return false;
}
