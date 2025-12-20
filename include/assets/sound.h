#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Sound {
	uint8_t* data; // owned by SDL_LoadWAV; free via sound_destroy
	uint32_t len;
	int sample_rate;
	int channels;
} Sound;

void sound_destroy(Sound* self);

bool sound_load_wav(Sound* out, const char* path);
