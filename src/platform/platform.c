#include "platform/platform.h"

#include "core/log.h"

#include <SDL.h>

bool platform_init(const PlatformConfig* cfg) {
	(void)cfg;
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0) {
		log_error("SDL_Init failed: %s", SDL_GetError());
		return false;
	}
	return true;
}

void platform_shutdown(void) {
	SDL_Quit();
}
