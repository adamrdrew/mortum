#include "core/config.h"

static CoreConfig g_cfg = {
	.internal_width = 640,
	.internal_height = 400,
	.fov_deg = 75.0f,
};

const CoreConfig* core_config_get(void) {
	return &g_cfg;
}
