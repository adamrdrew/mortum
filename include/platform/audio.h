#pragma once

#include <stdbool.h>

typedef struct Audio {
	bool initialized;
} Audio;

bool audio_init(Audio* self);
void audio_shutdown(Audio* self);
