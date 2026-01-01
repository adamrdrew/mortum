#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "assets/asset_paths.h"

#define HUD_MAX_WIDGETS 8

typedef enum HudHeightMode {
	HUD_HEIGHT_CLASSIC = 0,
} HudHeightMode;

typedef enum HudBackgroundMode {
	HUD_BACKGROUND_COLOR = 0,
	HUD_BACKGROUND_IMAGE = 1,
} HudBackgroundMode;

typedef struct HudBackground {
	HudBackgroundMode mode;
	uint32_t color_abgr;
	char image[128]; // safe relpath under Assets/Images/... (e.g. "HUD/bar.png")
} HudBackground;

typedef struct HudBevel {
	bool enabled;
	uint32_t hi_abgr;
	uint32_t lo_abgr;
	int thickness_px;
} HudBevel;

typedef struct HudShadow {
	bool enabled;
	int offset_x;
	int offset_y;
	uint32_t color_abgr;
} HudShadow;

typedef struct HudTextFit {
	float min_scale;
	float max_scale;
} HudTextFit;

typedef struct HudTextStyle {
	uint32_t color_abgr;
	uint32_t accent_color_abgr;
	int padding_x;
	int padding_y;
	HudTextFit fit;
	char font_file[64]; // optional; filename-only under Assets/Fonts/
} HudTextStyle;

typedef struct HudPanelStyle {
	HudBackground background;
	HudBevel bevel;
	HudShadow shadow;
	HudTextStyle text;
} HudPanelStyle;

typedef struct HudBarConfig {
	HudHeightMode height_mode;
	int padding_px;
	int gap_px;
	HudBackground background;
	HudBevel bevel;
} HudBarConfig;

typedef enum HudWidgetKind {
	HUD_WIDGET_HEALTH = 0,
	HUD_WIDGET_MORTUM = 1,
	HUD_WIDGET_AMMO = 2,
	HUD_WIDGET_EQUIPPED_WEAPON = 3,
	HUD_WIDGET_KEYS = 4,
	HUD_WIDGET_COUNT,
} HudWidgetKind;

typedef struct HudWidgetSpec {
	HudWidgetKind kind;
} HudWidgetSpec;

typedef struct HudAsset {
	int version;
	HudBarConfig bar;
	HudPanelStyle panel;
	int widget_count;
	HudWidgetSpec widgets[HUD_MAX_WIDGETS];
} HudAsset;

// Loads and validates Assets/HUD/<filename>.
// On failure, logs a clear error and returns false.
bool hud_asset_load(HudAsset* out, const AssetPaths* paths, const char* filename);
