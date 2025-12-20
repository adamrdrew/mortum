#include "platform/time.h"

#include <SDL.h>

double platform_time_seconds(void) {
	static uint64_t freq = 0;
	if (freq == 0) {
		freq = (uint64_t)SDL_GetPerformanceFrequency();
	}
	uint64_t t = (uint64_t)SDL_GetPerformanceCounter();
	return (double)t / (double)freq;
}
