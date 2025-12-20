#pragma once

#include <stdbool.h>

typedef struct PlatformConfig {
	bool enable_audio;
} PlatformConfig;

bool platform_init(const PlatformConfig* cfg);
void platform_shutdown(void);
