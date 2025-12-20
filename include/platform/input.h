#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Input {
	bool quit_requested;
	bool keys_down[512];
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
