#pragma once

#include <stdint.h>

typedef struct CoreConfig {
	int internal_width;
	int internal_height;
	float fov_deg;
} CoreConfig;

const CoreConfig* core_config_get(void);
