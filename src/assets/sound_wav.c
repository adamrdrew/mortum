#include "assets/sound.h"

#include "core/log.h"

#include <SDL.h>
#include <string.h>

void sound_destroy(Sound* self) {
	if (self->data) {
		SDL_FreeWAV(self->data);
		self->data = NULL;
	}
	self->len = 0;
	self->sample_rate = 0;
	self->channels = 0;
}

bool sound_load_wav(Sound* out, const char* path) {
	memset(out, 0, sizeof(*out));
	SDL_AudioSpec spec;
	SDL_zero(spec);
	if (!SDL_LoadWAV(path, &spec, &out->data, &out->len)) {
		log_error("SDL_LoadWAV failed for %s: %s", path, SDL_GetError());
		return false;
	}
	out->sample_rate = spec.freq;
	out->channels = spec.channels;
	return true;
}
