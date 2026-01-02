#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "assets/asset_paths.h"

typedef enum ConfigLoadMode {
	CONFIG_LOAD_STARTUP = 0,
	CONFIG_LOAD_RELOAD = 1,
} ConfigLoadMode;

typedef struct InputBindingsConfig {
	// Each action can have a primary/secondary scancode.
	//
	// open_main_menu is a single scancode used to open/dismiss the main menu.
	// (Default: Tab)
	int open_main_menu;
	//
	// release_mouse is a single scancode used to release mouse capture.
	// (Default: Escape)
	int release_mouse;
	int forward_primary;
	int forward_secondary;
	int back_primary;
	int back_secondary;
	int left_primary;
	int left_secondary;
	int right_primary;
	int right_secondary;
	int dash_primary;
	int dash_secondary;
	int action_primary;
	int action_secondary;
	int use_primary;
	int use_secondary;
	int weapon_slot_1;
	int weapon_slot_2;
	int weapon_slot_3;
	int weapon_slot_4;
	int weapon_slot_5;
	int weapon_prev;
	int weapon_next;
} InputBindingsConfig;

typedef struct PlayerTuningConfig {
	float mouse_sens_deg_per_px;
	float move_speed;
	float dash_distance;
	float dash_cooldown_s;

	float weapon_view_bob_smooth_rate;
	float weapon_view_bob_phase_rate;
	float weapon_view_bob_phase_base;
	float weapon_view_bob_phase_amp;
} PlayerTuningConfig;

typedef struct FootstepsConfig {
	bool enabled;
	float min_speed;
	float interval_s;
	int variant_count;
	char filename_pattern[64];
	float gain;
} FootstepsConfig;

typedef struct LightingConfig {
	bool enabled;
	float fog_start;
	float fog_end;
	float ambient_scale;
	float min_visibility;
	int quantize_steps;
	float quantize_low_cutoff;
} LightingConfig;

typedef struct WeaponBalanceConfig {
	int ammo_per_shot;
	float shot_cooldown_s;
	int pellets;
	float spread_deg;
	float proj_speed;
	float proj_radius;
	float proj_life_s;
	int proj_damage;
} WeaponBalanceConfig;

typedef struct WeaponViewAnimConfig {
	float shoot_anim_fps;
	int shoot_anim_frames;
} WeaponViewAnimConfig;

typedef struct WeaponSfxConfig {
	char handgun_shot[64];
	char shotgun_shot[64];
	char rifle_shot[64];
	char smg_shot[64];
	char rocket_shot[64];
	float shot_gain;
} WeaponSfxConfig;

typedef struct WeaponsConfig {
	WeaponBalanceConfig handgun;
	WeaponBalanceConfig shotgun;
	WeaponBalanceConfig rifle;
	WeaponBalanceConfig smg;
	WeaponBalanceConfig rocket;
	WeaponViewAnimConfig view;
	WeaponSfxConfig sfx;
} WeaponsConfig;

typedef struct WindowConfig {
	char title[64];
	int width;
	int height;
	bool vsync;
	bool grab_mouse;
	bool relative_mouse;
} WindowConfig;

typedef struct RenderConfig {
	int internal_width;
	int internal_height;
	float fov_deg;
	bool point_lights_enabled;
	LightingConfig lighting;
} RenderConfig;

typedef struct AudioConfig {
	bool enabled;
	float sfx_master_volume;
	float sfx_atten_min_dist;
	float sfx_atten_max_dist;
	int sfx_device_freq;
	int sfx_device_buffer_samples;
} AudioConfig;

typedef struct ContentConfig {
	char boot_timeline[64];
} ContentConfig;

typedef struct UiFontConfig {
	char file[64];
	int size_px;
	int atlas_size;
} UiFontConfig;

typedef struct UiConfig {
	UiFontConfig font;
	struct {
		char file[64];
	} hud;
} UiConfig;

typedef struct CoreConfig {
	WindowConfig window;
	RenderConfig render;
	AudioConfig audio;
	ContentConfig content;
	UiConfig ui;
	InputBindingsConfig input;
	PlayerTuningConfig player;
	FootstepsConfig footsteps;
	WeaponsConfig weapons;
	float console_opacity;
} CoreConfig;

const CoreConfig* core_config_get(void);

// Loads and validates JSON config at `path`. Validation is always performed.
// On CONFIG_LOAD_STARTUP: returns false on failure and logs why (caller should exit).
// On CONFIG_LOAD_RELOAD: returns false on failure, logs why, and keeps previous config.
bool core_config_load_from_file(const char* path, const AssetPaths* assets, ConfigLoadMode mode);

typedef enum CoreConfigValueKind {
	CORE_CONFIG_VALUE_STRING = 0,
	CORE_CONFIG_VALUE_BOOL = 1,
	CORE_CONFIG_VALUE_NUMBER = 2,
} CoreConfigValueKind;

typedef enum CoreConfigSetStatus {
	CORE_CONFIG_SET_OK = 0,
	CORE_CONFIG_SET_UNKNOWN_KEY = 1,
	CORE_CONFIG_SET_TYPE_MISMATCH = 2,
	CORE_CONFIG_SET_INVALID_VALUE = 3,
} CoreConfigSetStatus;

// Validated in-memory config mutation. This is intended for the in-game console.
//
// `key_path` uses JSON-style paths, e.g. "audio.sfx_master_volume" or
// "input.bindings.dash".
//
// `provided_kind` should be how the console parsed the value token.
// For numbers, the function will parse as int/float depending on the target.
//
// On CORE_CONFIG_SET_TYPE_MISMATCH, `out_expected_kind` is set to what the key
// expects.
CoreConfigSetStatus core_config_try_set_by_path(
	const char* key_path,
	CoreConfigValueKind provided_kind,
	const char* value_str,
	CoreConfigValueKind* out_expected_kind);

