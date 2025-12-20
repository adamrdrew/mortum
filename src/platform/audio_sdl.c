#include "platform/audio.h"

#include "core/log.h"

#include <SDL.h>

bool audio_init(Audio* self) {
	self->initialized = false;

	SDL_AudioSpec want;
	SDL_zero(want);
	want.freq = 48000;
	want.format = AUDIO_F32;
	want.channels = 2;
	want.samples = 1024;

	SDL_AudioSpec have;
	SDL_zero(have);

	SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (dev == 0) {
		log_warn("SDL_OpenAudioDevice failed (audio disabled): %s", SDL_GetError());
		return true; // non-fatal
	}

	SDL_PauseAudioDevice(dev, 0);
	SDL_CloseAudioDevice(dev);

	self->initialized = true;
	return true;
}

void audio_shutdown(Audio* self) {
	(void)self;
}
