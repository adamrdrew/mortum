#include "core/config.h"

#include "assets/json.h"
#include "core/log.h"

#include <SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static CoreConfig g_cfg = {
	.window = {
		.title = "Mortum",
		.width = 1280,
		.height = 720,
		.vsync = true,
		.grab_mouse = true,
		.relative_mouse = true,
	},
	.render = {
		.internal_width = 640,
		.internal_height = 400,
		.fov_deg = 75.0f,
		.point_lights_enabled = true,
		.lighting = {
			.enabled = true,
			.fog_start = 6.0f,
			.fog_end = 28.0f,
			.ambient_scale = 0.45f,
			.min_visibility = 0.485f,
			.quantize_steps = 16,
			.quantize_low_cutoff = 0.08f,
		},
	},
	.audio = {
		.enabled = true,
		.sfx_master_volume = 1.0f,
		.sfx_atten_min_dist = 6.0f,
		.sfx_atten_max_dist = 28.0f,
		.sfx_device_freq = 48000,
		.sfx_device_buffer_samples = 1024,
	},
	.content = {
		.default_episode = "episode1.json",
	},
	.ui = {
		.font = {
			.file = "ProggyClean.ttf",
			.size_px = 15,
			.atlas_size = 512,
		},
	},
	.input = {
		.forward_primary = SDL_SCANCODE_W,
		.forward_secondary = SDL_SCANCODE_UP,
		.back_primary = SDL_SCANCODE_S,
		.back_secondary = SDL_SCANCODE_DOWN,
		.left_primary = SDL_SCANCODE_A,
		.left_secondary = SDL_SCANCODE_UNKNOWN,
		.right_primary = SDL_SCANCODE_D,
		.right_secondary = SDL_SCANCODE_UNKNOWN,
		.dash_primary = SDL_SCANCODE_LSHIFT,
		.dash_secondary = SDL_SCANCODE_RSHIFT,
		.action_primary = SDL_SCANCODE_SPACE,
		.action_secondary = SDL_SCANCODE_UNKNOWN,
		.use_primary = SDL_SCANCODE_F,
		.use_secondary = SDL_SCANCODE_UNKNOWN,
		.weapon_slot_1 = SDL_SCANCODE_1,
		.weapon_slot_2 = SDL_SCANCODE_2,
		.weapon_slot_3 = SDL_SCANCODE_3,
		.weapon_slot_4 = SDL_SCANCODE_4,
		.weapon_slot_5 = SDL_SCANCODE_5,
		.weapon_prev = SDL_SCANCODE_Q,
		.weapon_next = SDL_SCANCODE_E,
	},
	.player = {
		.mouse_sens_deg_per_px = 0.12f,
		.move_speed = 4.7f,
		.dash_distance = 0.85f,
		.dash_cooldown_s = 0.65f,
		.weapon_view_bob_smooth_rate = 8.0f,
		.weapon_view_bob_phase_rate = 10.0f,
		.weapon_view_bob_phase_base = 0.2f,
		.weapon_view_bob_phase_amp = 0.8f,
	},
	.footsteps = {
		.enabled = true,
		.min_speed = 0.15f,
		.interval_s = 0.35f,
		.variant_count = 17,
		.filename_pattern = "Footstep_Boot_Concrete-%03u.wav",
		.gain = 0.7f,
	},
	.weapons = {
		.handgun = {
			.ammo_per_shot = 1,
			.shot_cooldown_s = 0.18f,
			.pellets = 1,
			.spread_deg = 0.0f,
			.proj_speed = 7.0f,
			.proj_radius = 0.055f,
			.proj_life_s = 1.2f,
			.proj_damage = 25,
		},
		.shotgun = {
			.ammo_per_shot = 1,
			.shot_cooldown_s = 0.70f,
			.pellets = 6,
			.spread_deg = 14.0f,
			.proj_speed = 6.2f,
			.proj_radius = 0.050f,
			.proj_life_s = 0.75f,
			.proj_damage = 16,
		},
		.rifle = {
			.ammo_per_shot = 2,
			.shot_cooldown_s = 0.28f,
			.pellets = 1,
			.spread_deg = 1.5f,
			.proj_speed = 9.5f,
			.proj_radius = 0.045f,
			.proj_life_s = 1.4f,
			.proj_damage = 40,
		},
		.smg = {
			.ammo_per_shot = 1,
			.shot_cooldown_s = 0.08f,
			.pellets = 1,
			.spread_deg = 6.0f,
			.proj_speed = 7.8f,
			.proj_radius = 0.040f,
			.proj_life_s = 1.0f,
			.proj_damage = 14,
		},
		.rocket = {
			.ammo_per_shot = 4,
			.shot_cooldown_s = 0.95f,
			.pellets = 1,
			.spread_deg = 0.0f,
			.proj_speed = 5.5f,
			.proj_radius = 0.090f,
			.proj_life_s = 1.8f,
			.proj_damage = 120,
		},
		.view = {
			.shoot_anim_fps = 30.0f,
			.shoot_anim_frames = 6,
		},
		.sfx = {
			.handgun_shot = "Sniper_Shot-001.wav",
			.shotgun_shot = "Shotgun_Shot-001.wav",
			.rifle_shot = "Sniper_Shot-002.wav",
			.smg_shot = "Sniper_Shot-003.wav",
			.rocket_shot = "Rocket_Shot-001.wav",
			.shot_gain = 1.0f,
		},
	},
};

const CoreConfig* core_config_get(void) {
	return &g_cfg;
}

static bool file_exists(const char* path) {
	if (!path || path[0] == '\0') {
		return false;
	}
	FILE* f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	fclose(f);
	return true;
}

static bool json_get_bool_any(const JsonDoc* doc, int tok, bool* out) {
	if (!doc || !out || tok < 0 || tok >= doc->token_count) {
		return false;
	}
	StringView sv = json_token_sv(doc, tok);
	if (sv.len == 4 && strncmp(sv.data, "true", 4) == 0) {
		*out = true;
		return true;
	}
	if (sv.len == 5 && strncmp(sv.data, "false", 5) == 0) {
		*out = false;
		return true;
	}
	// Accept 0/1 as a convenience.
	int v = 0;
	if (json_get_int(doc, tok, &v) && (v == 0 || v == 1)) {
		*out = (v != 0);
		return true;
	}
	return false;
}

static bool json_get_float_any(const JsonDoc* doc, int tok, float* out) {
	double d = 0.0;
	if (!json_get_double(doc, tok, &d)) {
		return false;
	}
	*out = (float)d;
	return true;
}

static void copy_sv_to_buf(char* dst, size_t dst_size, StringView sv) {
	if (!dst || dst_size == 0) {
		return;
	}
	size_t n = sv.len;
	if (n >= dst_size) {
		n = dst_size - 1;
	}
	memcpy(dst, sv.data, n);
	dst[n] = '\0';
}

static int tok_next_local(const JsonDoc* doc, int tok) {
	if (!doc || tok < 0 || tok >= doc->token_count) {
		return tok + 1;
	}
	jsmntok_t* t = &doc->tokens[tok];
	if (t->type == JSMN_STRING || t->type == JSMN_PRIMITIVE) {
		return tok + 1;
	}
	int i = tok + 1;
	for (int n = 0; n < t->size; n++) {
		i = tok_next_local(doc, i);
	}
	return i;
}

static void warn_unknown_keys(const JsonDoc* doc, int obj_tok, const char* const* allowed, int allowed_count, const char* prefix) {
	if (!json_token_is_object(doc, obj_tok)) {
		return;
	}
	jsmntok_t* obj = &doc->tokens[obj_tok];
	int pair_count = obj->size / 2;
	int i = obj_tok + 1;
	for (int pair = 0; pair < pair_count; pair++) {
		if (i < 0 || i + 1 >= doc->token_count) {
			break;
		}
		int key_tok = i;
		int val_tok = i + 1;
		StringView k = json_token_sv(doc, key_tok);
		bool known = false;
		for (int a = 0; a < allowed_count; a++) {
			const char* ak = allowed[a];
			size_t akn = strlen(ak);
			if (k.len == akn && strncmp(k.data, ak, akn) == 0) {
				known = true;
				break;
			}
		}
		if (!known) {
			char keybuf[96];
			copy_sv_to_buf(keybuf, sizeof(keybuf), k);
			if (prefix && prefix[0] != '\0') {
				log_warn("Unknown config path: %s.%s", prefix, keybuf);
			} else {
				log_warn("Unknown config path: %s", keybuf);
			}
		}
		i = tok_next_local(doc, val_tok);
	}
}

static bool parse_scancode(const JsonDoc* doc, int tok, int* out_scancode) {
	if (!out_scancode) {
		return false;
	}
	int i = 0;
	if (json_get_int(doc, tok, &i)) {
		if (i >= 0 && i < 512) {
			*out_scancode = i;
			return true;
		}
		return false;
	}
	StringView sv;
	if (!json_get_string(doc, tok, &sv)) {
		return false;
	}
	char name[64];
	copy_sv_to_buf(name, sizeof(name), sv);
	const char* p = name;
	const char* prefix = "SDL_SCANCODE_";
	if (strncmp(name, prefix, strlen(prefix)) == 0) {
		p = name + strlen(prefix);
	}

	// Extra aliases to make configs more mod-friendly (SDL naming differs a bit per platform/version).
	char lower[64];
	{
		size_t n = strlen(p);
		if (n >= sizeof(lower)) {
			n = sizeof(lower) - 1;
		}
		for (size_t i = 0; i < n; i++) {
			lower[i] = (char)tolower((unsigned char)p[i]);
		}
		lower[n] = '\0';
	}
	if (strcmp(lower, "grave") == 0 || strcmp(lower, "backquote") == 0 || strcmp(lower, "`") == 0) {
		*out_scancode = (int)SDL_SCANCODE_GRAVE;
		return true;
	}
	if (strcmp(lower, "escape") == 0 || strcmp(lower, "esc") == 0) {
		*out_scancode = (int)SDL_SCANCODE_ESCAPE;
		return true;
	}
	SDL_Scancode sc = SDL_GetScancodeFromName(p);
	if (sc == SDL_SCANCODE_UNKNOWN) {
		return false;
	}
	*out_scancode = (int)sc;
	return true;
}

static bool parse_scancode_1_or_2(const JsonDoc* doc, int tok, int* out_primary, int* out_secondary) {
	if (!out_primary || !out_secondary) {
		return false;
	}
	*out_primary = (int)SDL_SCANCODE_UNKNOWN;
	*out_secondary = (int)SDL_SCANCODE_UNKNOWN;
	if (!doc || tok < 0 || tok >= doc->token_count) {
		return false;
	}
	if (json_token_is_array(doc, tok)) {
		int count = json_array_size(doc, tok);
		if (count < 1 || count > 2) {
			return false;
		}
		int t0 = json_array_nth(doc, tok, 0);
		int sc0 = 0;
		if (!parse_scancode(doc, t0, &sc0)) {
			return false;
		}
		*out_primary = sc0;
		if (count == 2) {
			int t1 = json_array_nth(doc, tok, 1);
			int sc1 = 0;
			if (!parse_scancode(doc, t1, &sc1)) {
				return false;
			}
			*out_secondary = sc1;
		}
		return true;
	}
	int sc = 0;
	if (!parse_scancode(doc, tok, &sc)) {
		return false;
	}
	*out_primary = sc;
	*out_secondary = (int)SDL_SCANCODE_UNKNOWN;
	return true;
}

static bool validate_asset_file(const AssetPaths* assets, const char* subdir, const char* filename, const char* config_path, const char* field, bool* ok) {
	if (!ok) {
		return false;
	}
	if (!assets || !subdir || !filename || filename[0] == '\0') {
		*ok = false;
		log_error("Config: %s: %s missing", config_path ? config_path : "(unknown)", field);
		return false;
	}
	char* full = asset_path_join(assets, subdir, filename);
	if (!full) {
		*ok = false;
		log_error("Config: %s: %s out of memory", config_path ? config_path : "(unknown)", field);
		return false;
	}
	bool exists = file_exists(full);
	if (!exists) {
		*ok = false;
		log_error("Config: %s: %s asset not found: %s", config_path ? config_path : "(unknown)", field, full);
	}
	free(full);
	return exists;
}

bool core_config_load_from_file(const char* path, const AssetPaths* assets, ConfigLoadMode mode) {
	if (!path || path[0] == '\0') {
		log_error("Config: no path provided");
		return false;
	}

	JsonDoc doc;
	if (!json_doc_load_file(&doc, path)) {
		return false;
	}

	bool ok = true;
	CoreConfig next = g_cfg; // start with current (defaults or last good)

	if (doc.token_count < 1 || !json_token_is_object(&doc, 0)) {
		log_error("Config: %s: root must be an object", path);
		ok = false;
	}

	if (ok) {
		static const char* const allowed_root[] = {"window", "render", "audio", "content", "ui", "input", "player", "footsteps", "weapons"};
		warn_unknown_keys(&doc, 0, allowed_root, (int)(sizeof(allowed_root) / sizeof(allowed_root[0])), "");

		// window
		int t_window = -1;
		if (json_object_get(&doc, 0, "window", &t_window)) {
			if (!json_token_is_object(&doc, t_window)) {
				log_error("Config: %s: window must be an object", path);
				ok = false;
			} else {
				static const char* const allowed_window[] = {"title", "width", "height", "vsync", "grab_mouse", "relative_mouse"};
				warn_unknown_keys(&doc, t_window, allowed_window, (int)(sizeof(allowed_window) / sizeof(allowed_window[0])), "window");

				int t_title = -1;
				if (json_object_get(&doc, t_window, "title", &t_title)) {
					StringView sv;
					if (!json_get_string(&doc, t_title, &sv)) {
						log_error("Config: %s: window.title must be a string", path);
						ok = false;
					} else {
						copy_sv_to_buf(next.window.title, sizeof(next.window.title), sv);
					}
				}
				int t_w = -1;
				if (json_object_get(&doc, t_window, "width", &t_w)) {
					int v = 0;
					if (!json_get_int(&doc, t_w, &v) || v < 320 || v > 8192) {
						log_error("Config: %s: window.width must be int in [320..8192]", path);
						ok = false;
					} else {
						next.window.width = v;
					}
				}
				int t_h = -1;
				if (json_object_get(&doc, t_window, "height", &t_h)) {
					int v = 0;
					if (!json_get_int(&doc, t_h, &v) || v < 200 || v > 8192) {
						log_error("Config: %s: window.height must be int in [200..8192]", path);
						ok = false;
					} else {
						next.window.height = v;
					}
				}
				int t_vsync = -1;
				if (json_object_get(&doc, t_window, "vsync", &t_vsync)) {
					bool b = false;
					if (!json_get_bool_any(&doc, t_vsync, &b)) {
						log_error("Config: %s: window.vsync must be bool", path);
						ok = false;
					} else {
						next.window.vsync = b;
					}
				}
				int t_grab = -1;
				if (json_object_get(&doc, t_window, "grab_mouse", &t_grab)) {
					bool b = false;
					if (!json_get_bool_any(&doc, t_grab, &b)) {
						log_error("Config: %s: window.grab_mouse must be bool", path);
						ok = false;
					} else {
						next.window.grab_mouse = b;
					}
				}
				int t_rel = -1;
				if (json_object_get(&doc, t_window, "relative_mouse", &t_rel)) {
					bool b = false;
					if (!json_get_bool_any(&doc, t_rel, &b)) {
						log_error("Config: %s: window.relative_mouse must be bool", path);
						ok = false;
					} else {
						next.window.relative_mouse = b;
					}
				}
			}
		}

		// render
		int t_render = -1;
		if (json_object_get(&doc, 0, "render", &t_render)) {
			if (!json_token_is_object(&doc, t_render)) {
				log_error("Config: %s: render must be an object", path);
				ok = false;
			} else {
				static const char* const allowed_render[] = {"internal_width", "internal_height", "fov_deg", "point_lights_enabled", "lighting"};
				warn_unknown_keys(&doc, t_render, allowed_render, (int)(sizeof(allowed_render) / sizeof(allowed_render[0])), "render");

				int t_iw = -1;
				if (json_object_get(&doc, t_render, "internal_width", &t_iw)) {
					int v = 0;
					if (!json_get_int(&doc, t_iw, &v) || v < 160 || v > 4096) {
						log_error("Config: %s: render.internal_width must be int in [160..4096]", path);
						ok = false;
					} else {
						next.render.internal_width = v;
					}
				}
				int t_ih = -1;
				if (json_object_get(&doc, t_render, "internal_height", &t_ih)) {
					int v = 0;
					if (!json_get_int(&doc, t_ih, &v) || v < 120 || v > 4096) {
						log_error("Config: %s: render.internal_height must be int in [120..4096]", path);
						ok = false;
					} else {
						next.render.internal_height = v;
					}
				}
				int t_fov = -1;
				if (json_object_get(&doc, t_render, "fov_deg", &t_fov)) {
					float v = 0.0f;
					if (!json_get_float_any(&doc, t_fov, &v) || v < 30.0f || v > 140.0f) {
						log_error("Config: %s: render.fov_deg must be number in [30..140]", path);
						ok = false;
					} else {
						next.render.fov_deg = v;
					}
				}
				int t_pl = -1;
				if (json_object_get(&doc, t_render, "point_lights_enabled", &t_pl)) {
					bool b = false;
					if (!json_get_bool_any(&doc, t_pl, &b)) {
						log_error("Config: %s: render.point_lights_enabled must be bool", path);
						ok = false;
					} else {
						next.render.point_lights_enabled = b;
					}
				}

				int t_light = -1;
				if (json_object_get(&doc, t_render, "lighting", &t_light)) {
					if (!json_token_is_object(&doc, t_light)) {
						log_error("Config: %s: render.lighting must be an object", path);
						ok = false;
					} else {
						static const char* const allowed_light[] = {
							"enabled",
							"fog_start",
							"fog_end",
							"ambient_scale",
							"min_visibility",
							"quantize_steps",
							"quantize_low_cutoff",
						};
						warn_unknown_keys(&doc, t_light, allowed_light, (int)(sizeof(allowed_light) / sizeof(allowed_light[0])), "render.lighting");

						int t_en = -1;
						if (json_object_get(&doc, t_light, "enabled", &t_en)) {
							bool b = false;
							if (!json_get_bool_any(&doc, t_en, &b)) {
								log_error("Config: %s: render.lighting.enabled must be bool", path);
								ok = false;
							} else {
								next.render.lighting.enabled = b;
							}
						}
						int t_fs = -1;
						if (json_object_get(&doc, t_light, "fog_start", &t_fs)) {
							float v = 0.0f;
							if (!json_get_float_any(&doc, t_fs, &v) || v < 0.0f || v > 9999.0f) {
								log_error("Config: %s: render.lighting.fog_start must be number in [0..9999]", path);
								ok = false;
							} else {
								next.render.lighting.fog_start = v;
							}
						}
						int t_fe = -1;
						if (json_object_get(&doc, t_light, "fog_end", &t_fe)) {
							float v = 0.0f;
							if (!json_get_float_any(&doc, t_fe, &v) || v < 0.0f || v > 9999.0f) {
								log_error("Config: %s: render.lighting.fog_end must be number in [0..9999]", path);
								ok = false;
							} else {
								next.render.lighting.fog_end = v;
							}
						}
						if (next.render.lighting.fog_end < next.render.lighting.fog_start) {
							log_error("Config: %s: render.lighting.fog_end must be >= fog_start", path);
							ok = false;
						}
						int t_as = -1;
						if (json_object_get(&doc, t_light, "ambient_scale", &t_as)) {
							float v = 0.0f;
							if (!json_get_float_any(&doc, t_as, &v) || v < 0.0f || v > 2.0f) {
								log_error("Config: %s: render.lighting.ambient_scale must be number in [0..2]", path);
								ok = false;
							} else {
								next.render.lighting.ambient_scale = v;
							}
						}
						int t_mv = -1;
						if (json_object_get(&doc, t_light, "min_visibility", &t_mv)) {
							float v = 0.0f;
							if (!json_get_float_any(&doc, t_mv, &v) || v < 0.0f || v > 1.0f) {
								log_error("Config: %s: render.lighting.min_visibility must be number in [0..1]", path);
								ok = false;
							} else {
								next.render.lighting.min_visibility = v;
							}
						}
						int t_qs = -1;
						if (json_object_get(&doc, t_light, "quantize_steps", &t_qs)) {
							int v = 0;
							if (!json_get_int(&doc, t_qs, &v) || v < 0 || v > 256) {
								log_error("Config: %s: render.lighting.quantize_steps must be int in [0..256]", path);
								ok = false;
							} else {
								next.render.lighting.quantize_steps = v;
							}
						}
						int t_qc = -1;
						if (json_object_get(&doc, t_light, "quantize_low_cutoff", &t_qc)) {
							float v = 0.0f;
							if (!json_get_float_any(&doc, t_qc, &v) || v < 0.0f || v > 1.0f) {
								log_error("Config: %s: render.lighting.quantize_low_cutoff must be number in [0..1]", path);
								ok = false;
							} else {
								next.render.lighting.quantize_low_cutoff = v;
							}
						}
					}
				}
			}
		}

		// audio
		int t_audio = -1;
		if (json_object_get(&doc, 0, "audio", &t_audio)) {
			if (!json_token_is_object(&doc, t_audio)) {
				log_error("Config: %s: audio must be an object", path);
				ok = false;
			} else {
				static const char* const allowed_audio[] = {"enabled", "sfx_master_volume", "sfx_atten_min_dist", "sfx_atten_max_dist", "sfx_device_freq", "sfx_device_buffer_samples"};
				warn_unknown_keys(&doc, t_audio, allowed_audio, (int)(sizeof(allowed_audio) / sizeof(allowed_audio[0])), "audio");
				int t_en = -1;
				if (json_object_get(&doc, t_audio, "enabled", &t_en)) {
					bool b = false;
					if (!json_get_bool_any(&doc, t_en, &b)) {
						log_error("Config: %s: audio.enabled must be bool", path);
						ok = false;
					} else {
						next.audio.enabled = b;
					}
				}
				int t_mv = -1;
				if (json_object_get(&doc, t_audio, "sfx_master_volume", &t_mv)) {
					float v = 0.0f;
					if (!json_get_float_any(&doc, t_mv, &v) || v < 0.0f || v > 1.0f) {
						log_error("Config: %s: audio.sfx_master_volume must be number in [0..1]", path);
						ok = false;
					} else {
						next.audio.sfx_master_volume = v;
					}
				}
				int t_amin = -1;
				if (json_object_get(&doc, t_audio, "sfx_atten_min_dist", &t_amin)) {
					float v = 0.0f;
					if (!json_get_float_any(&doc, t_amin, &v) || v < 0.0f || v > 9999.0f) {
						log_error("Config: %s: audio.sfx_atten_min_dist must be number in [0..9999]", path);
						ok = false;
					} else {
						next.audio.sfx_atten_min_dist = v;
					}
				}
				int t_amax = -1;
				if (json_object_get(&doc, t_audio, "sfx_atten_max_dist", &t_amax)) {
					float v = 0.0f;
					if (!json_get_float_any(&doc, t_amax, &v) || v < 0.0f || v > 9999.0f) {
						log_error("Config: %s: audio.sfx_atten_max_dist must be number in [0..9999]", path);
						ok = false;
					} else {
						next.audio.sfx_atten_max_dist = v;
					}
				}
				if (next.audio.sfx_atten_max_dist < next.audio.sfx_atten_min_dist) {
					log_error("Config: %s: audio.sfx_atten_max_dist must be >= sfx_atten_min_dist", path);
					ok = false;
				}
				int t_freq = -1;
				if (json_object_get(&doc, t_audio, "sfx_device_freq", &t_freq)) {
					int v = 0;
					if (!json_get_int(&doc, t_freq, &v) || v < 8000 || v > 192000) {
						log_error("Config: %s: audio.sfx_device_freq must be int in [8000..192000]", path);
						ok = false;
					} else {
						next.audio.sfx_device_freq = v;
					}
				}
				int t_buf = -1;
				if (json_object_get(&doc, t_audio, "sfx_device_buffer_samples", &t_buf)) {
					int v = 0;
					if (!json_get_int(&doc, t_buf, &v) || v < 128 || v > 8192) {
						log_error("Config: %s: audio.sfx_device_buffer_samples must be int in [128..8192]", path);
						ok = false;
					} else {
						next.audio.sfx_device_buffer_samples = v;
					}
				}
			}
		}

		// content
		int t_content = -1;
		if (json_object_get(&doc, 0, "content", &t_content)) {
			if (!json_token_is_object(&doc, t_content)) {
				log_error("Config: %s: content must be an object", path);
				ok = false;
			} else {
				static const char* const allowed_content[] = {"default_episode"};
				warn_unknown_keys(&doc, t_content, allowed_content, (int)(sizeof(allowed_content) / sizeof(allowed_content[0])), "content");
				int t_ep = -1;
				if (json_object_get(&doc, t_content, "default_episode", &t_ep)) {
					StringView sv;
					if (!json_get_string(&doc, t_ep, &sv) || sv.len == 0) {
						log_error("Config: %s: content.default_episode must be a non-empty string", path);
						ok = false;
					} else {
						copy_sv_to_buf(next.content.default_episode, sizeof(next.content.default_episode), sv);
					}
				}
			}
		}

		// ui
		int t_ui = -1;
		if (json_object_get(&doc, 0, "ui", &t_ui)) {
			if (!json_token_is_object(&doc, t_ui)) {
				log_error("Config: %s: ui must be an object", path);
				ok = false;
			} else {
				static const char* const allowed_ui[] = {"font"};
				warn_unknown_keys(&doc, t_ui, allowed_ui, (int)(sizeof(allowed_ui) / sizeof(allowed_ui[0])), "ui");

				int t_font = -1;
				if (json_object_get(&doc, t_ui, "font", &t_font)) {
					if (!json_token_is_object(&doc, t_font)) {
						log_error("Config: %s: ui.font must be an object", path);
						ok = false;
					} else {
						static const char* const allowed_font[] = {"file", "size", "atlas_size"};
						warn_unknown_keys(&doc, t_font, allowed_font, (int)(sizeof(allowed_font) / sizeof(allowed_font[0])), "ui.font");

						int t_file = -1;
						if (json_object_get(&doc, t_font, "file", &t_file)) {
							StringView sv;
							if (!json_get_string(&doc, t_file, &sv) || sv.len == 0) {
								log_error("Config: %s: ui.font.file must be a non-empty string", path);
								ok = false;
							} else {
								// Always loaded from Assets/Fonts/. Disallow path separators.
								bool has_sep = false;
								for (size_t i = 0; i < sv.len; i++) {
									char ch = sv.data[i];
									if (ch == '/' || ch == '\\') {
										has_sep = true;
										break;
									}
								}
								if (has_sep) {
									log_error("Config: %s: ui.font.file must be a filename under Assets/Fonts/ (no path separators)", path);
									ok = false;
								} else {
									copy_sv_to_buf(next.ui.font.file, sizeof(next.ui.font.file), sv);
								}
							}
						}

						int t_size = -1;
						if (json_object_get(&doc, t_font, "size", &t_size)) {
							int v = 0;
							if (!json_get_int(&doc, t_size, &v) || v < 6 || v > 96) {
								log_error("Config: %s: ui.font.size must be int in [6..96]", path);
								ok = false;
							} else {
								next.ui.font.size_px = v;
							}
						}

						int t_as = -1;
						if (json_object_get(&doc, t_font, "atlas_size", &t_as)) {
							int v = 0;
							if (!json_get_int(&doc, t_as, &v) || v < 128 || v > 4096) {
								log_error("Config: %s: ui.font.atlas_size must be int in [128..4096]", path);
								ok = false;
							} else {
								next.ui.font.atlas_size = v;
							}
						}
					}
				}
			}
		}

		// input
		int t_input = -1;
		if (json_object_get(&doc, 0, "input", &t_input)) {
			if (!json_token_is_object(&doc, t_input)) {
				log_error("Config: %s: input must be an object", path);
				ok = false;
			} else {
				static const char* const allowed_input[] = {"bindings"};
				warn_unknown_keys(&doc, t_input, allowed_input, (int)(sizeof(allowed_input) / sizeof(allowed_input[0])), "input");
				int t_bind = -1;
				if (json_object_get(&doc, t_input, "bindings", &t_bind)) {
					if (!json_token_is_object(&doc, t_bind)) {
						log_error("Config: %s: input.bindings must be an object", path);
						ok = false;
					} else {
						static const char* const allowed_bind[] = {
							"forward",
							"back",
							"left",
							"right",
							"dash",
							"action",
							"use",
							"weapon_slot_1",
							"weapon_slot_2",
							"weapon_slot_3",
							"weapon_slot_4",
							"weapon_slot_5",
							"weapon_prev",
							"weapon_next",
						};
						warn_unknown_keys(&doc, t_bind, allowed_bind, (int)(sizeof(allowed_bind) / sizeof(allowed_bind[0])), "input.bindings");

						#define PARSE_BIND2(field, json_name) \
							do { \
								int t = -1; \
								if (json_object_get(&doc, t_bind, (json_name), &t)) { \
									int p = 0, s = 0; \
									if (!parse_scancode_1_or_2(&doc, t, &p, &s)) { \
										log_error("Config: %s: input.bindings.%s must be a scancode, key name string, or [primary, secondary]", path, (json_name)); \
										ok = false; \
									} else { \
										next.input.field##_primary = p; \
										next.input.field##_secondary = s; \
									} \
								} \
							} while (0)
						#define PARSE_BIND1(field, json_name) \
							do { \
								int t = -1; \
								if (json_object_get(&doc, t_bind, (json_name), &t)) { \
									int sc = 0; \
									if (!parse_scancode(&doc, t, &sc)) { \
										log_error("Config: %s: input.bindings.%s must be a scancode int or key name string", path, (json_name)); \
										ok = false; \
									} else { \
										next.input.field = sc; \
									} \
								} \
							} while (0)

						PARSE_BIND2(forward, "forward");
						PARSE_BIND2(back, "back");
						PARSE_BIND2(left, "left");
						PARSE_BIND2(right, "right");
						PARSE_BIND2(dash, "dash");
						PARSE_BIND2(action, "action");
						PARSE_BIND2(use, "use");
						PARSE_BIND1(weapon_slot_1, "weapon_slot_1");
						PARSE_BIND1(weapon_slot_2, "weapon_slot_2");
						PARSE_BIND1(weapon_slot_3, "weapon_slot_3");
						PARSE_BIND1(weapon_slot_4, "weapon_slot_4");
						PARSE_BIND1(weapon_slot_5, "weapon_slot_5");
						PARSE_BIND1(weapon_prev, "weapon_prev");
						PARSE_BIND1(weapon_next, "weapon_next");


						#undef PARSE_BIND1
						#undef PARSE_BIND2
					}
				}
			}

			// player tuning
			int t_player = -1;
			if (json_object_get(&doc, 0, "player", &t_player)) {
				if (!json_token_is_object(&doc, t_player)) {
					log_error("Config: %s: player must be an object", path);
					ok = false;
				} else {
					static const char* const allowed_player[] = {
						"mouse_sens_deg_per_px",
						"move_speed",
						"dash_distance",
						"dash_cooldown_s",
						"weapon_view_bob_smooth_rate",
						"weapon_view_bob_phase_rate",
						"weapon_view_bob_phase_base",
						"weapon_view_bob_phase_amp",
					};
					warn_unknown_keys(&doc, t_player, allowed_player, (int)(sizeof(allowed_player) / sizeof(allowed_player[0])), "player");

					int t = -1;
					if (json_object_get(&doc, t_player, "mouse_sens_deg_per_px", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 10.0f) {
							log_error("Config: %s: player.mouse_sens_deg_per_px must be number in [0..10]", path);
							ok = false;
						} else {
							next.player.mouse_sens_deg_per_px = v;
						}
					}
					if (json_object_get(&doc, t_player, "move_speed", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 100.0f) {
							log_error("Config: %s: player.move_speed must be number in [0..100]", path);
							ok = false;
						} else {
							next.player.move_speed = v;
						}
					}
					if (json_object_get(&doc, t_player, "dash_distance", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 100.0f) {
							log_error("Config: %s: player.dash_distance must be number in [0..100]", path);
							ok = false;
						} else {
							next.player.dash_distance = v;
						}
					}
					if (json_object_get(&doc, t_player, "dash_cooldown_s", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 60.0f) {
							log_error("Config: %s: player.dash_cooldown_s must be number in [0..60]", path);
							ok = false;
						} else {
							next.player.dash_cooldown_s = v;
						}
					}
					if (json_object_get(&doc, t_player, "weapon_view_bob_smooth_rate", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 100.0f) {
							log_error("Config: %s: player.weapon_view_bob_smooth_rate must be number in [0..100]", path);
							ok = false;
						} else {
							next.player.weapon_view_bob_smooth_rate = v;
						}
					}
					if (json_object_get(&doc, t_player, "weapon_view_bob_phase_rate", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 100.0f) {
							log_error("Config: %s: player.weapon_view_bob_phase_rate must be number in [0..100]", path);
							ok = false;
						} else {
							next.player.weapon_view_bob_phase_rate = v;
						}
					}
					if (json_object_get(&doc, t_player, "weapon_view_bob_phase_base", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 10.0f) {
							log_error("Config: %s: player.weapon_view_bob_phase_base must be number in [0..10]", path);
							ok = false;
						} else {
							next.player.weapon_view_bob_phase_base = v;
						}
					}
					if (json_object_get(&doc, t_player, "weapon_view_bob_phase_amp", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 10.0f) {
							log_error("Config: %s: player.weapon_view_bob_phase_amp must be number in [0..10]", path);
							ok = false;
						} else {
							next.player.weapon_view_bob_phase_amp = v;
						}
					}
				}
			}

			// footsteps
			int t_fs = -1;
			if (json_object_get(&doc, 0, "footsteps", &t_fs)) {
				if (!json_token_is_object(&doc, t_fs)) {
					log_error("Config: %s: footsteps must be an object", path);
					ok = false;
				} else {
					static const char* const allowed_fs[] = {"enabled", "min_speed", "interval_s", "variant_count", "filename_pattern", "gain"};
					warn_unknown_keys(&doc, t_fs, allowed_fs, (int)(sizeof(allowed_fs) / sizeof(allowed_fs[0])), "footsteps");
					int t = -1;
					if (json_object_get(&doc, t_fs, "enabled", &t)) {
						bool b = false;
						if (!json_get_bool_any(&doc, t, &b)) {
							log_error("Config: %s: footsteps.enabled must be bool", path);
							ok = false;
						} else {
							next.footsteps.enabled = b;
						}
					}
					if (json_object_get(&doc, t_fs, "min_speed", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 100.0f) {
							log_error("Config: %s: footsteps.min_speed must be number in [0..100]", path);
							ok = false;
						} else {
							next.footsteps.min_speed = v;
						}
					}
					if (json_object_get(&doc, t_fs, "interval_s", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 10.0f) {
							log_error("Config: %s: footsteps.interval_s must be number in [0..10]", path);
							ok = false;
						} else {
							next.footsteps.interval_s = v;
						}
					}
					if (json_object_get(&doc, t_fs, "variant_count", &t)) {
						int v = 0;
						if (!json_get_int(&doc, t, &v) || v < 0 || v > 999) {
							log_error("Config: %s: footsteps.variant_count must be int in [0..999]", path);
							ok = false;
						} else {
							next.footsteps.variant_count = v;
						}
					}
					if (json_object_get(&doc, t_fs, "filename_pattern", &t)) {
						StringView sv;
						if (!json_get_string(&doc, t, &sv) || sv.len == 0) {
							log_error("Config: %s: footsteps.filename_pattern must be a non-empty string", path);
							ok = false;
						} else {
							copy_sv_to_buf(next.footsteps.filename_pattern, sizeof(next.footsteps.filename_pattern), sv);
						}
					}
					if (json_object_get(&doc, t_fs, "gain", &t)) {
						float v = 0.0f;
						if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 1.0f) {
							log_error("Config: %s: footsteps.gain must be number in [0..1]", path);
							ok = false;
						} else {
							next.footsteps.gain = v;
						}
					}

					if (next.footsteps.filename_pattern[0] != '\0' && strstr(next.footsteps.filename_pattern, "%03") == NULL) {
						log_error("Config: %s: footsteps.filename_pattern should contain a printf-style %%03u placeholder", path);
						ok = false;
					}
				}
			}

			// weapons
			int t_weap = -1;
			if (json_object_get(&doc, 0, "weapons", &t_weap)) {
				if (!json_token_is_object(&doc, t_weap)) {
					log_error("Config: %s: weapons must be an object", path);
					ok = false;
				} else {
					static const char* const allowed_weap[] = {"balance", "view", "sfx"};
					warn_unknown_keys(&doc, t_weap, allowed_weap, (int)(sizeof(allowed_weap) / sizeof(allowed_weap[0])), "weapons");

					// weapons.balance
					int t_bal = -1;
					if (json_object_get(&doc, t_weap, "balance", &t_bal)) {
						if (!json_token_is_object(&doc, t_bal)) {
							log_error("Config: %s: weapons.balance must be an object", path);
							ok = false;
						} else {
							static const char* const allowed_bal[] = {"handgun", "shotgun", "rifle", "smg", "rocket"};
							warn_unknown_keys(&doc, t_bal, allowed_bal, (int)(sizeof(allowed_bal) / sizeof(allowed_bal[0])), "weapons.balance");

							#define PARSE_WEAPON_BAL(dst, name) \
								do { \
									int t_w = -1; \
									if (json_object_get(&doc, t_bal, (name), &t_w)) { \
										if (!json_token_is_object(&doc, t_w)) { \
											log_error("Config: %s: weapons.balance.%s must be an object", path, (name)); \
											ok = false; \
										} else { \
											static const char* const allowed_w[] = {"ammo_per_shot","shot_cooldown_s","pellets","spread_deg","proj_speed","proj_radius","proj_life_s","proj_damage"}; \
											char pfx[64]; \
											snprintf(pfx, sizeof(pfx), "weapons.balance.%s", (name)); \
											warn_unknown_keys(&doc, t_w, allowed_w, (int)(sizeof(allowed_w)/sizeof(allowed_w[0])), pfx); \
											int t = -1; \
											if (json_object_get(&doc, t_w, "ammo_per_shot", &t)) { int v=0; if (!json_get_int(&doc,t,&v) || v<0 || v>999) { log_error("Config: %s: %s.ammo_per_shot must be int in [0..999]", path, pfx); ok=false; } else { (dst).ammo_per_shot=v; } } \
											if (json_object_get(&doc, t_w, "shot_cooldown_s", &t)) { float v=0; if (!json_get_float_any(&doc,t,&v) || v<0.0f || v>60.0f) { log_error("Config: %s: %s.shot_cooldown_s must be number in [0..60]", path, pfx); ok=false; } else { (dst).shot_cooldown_s=v; } } \
											if (json_object_get(&doc, t_w, "pellets", &t)) { int v=0; if (!json_get_int(&doc,t,&v) || v<1 || v>128) { log_error("Config: %s: %s.pellets must be int in [1..128]", path, pfx); ok=false; } else { (dst).pellets=v; } } \
											if (json_object_get(&doc, t_w, "spread_deg", &t)) { float v=0; if (!json_get_float_any(&doc,t,&v) || v<0.0f || v>180.0f) { log_error("Config: %s: %s.spread_deg must be number in [0..180]", path, pfx); ok=false; } else { (dst).spread_deg=v; } } \
											if (json_object_get(&doc, t_w, "proj_speed", &t)) { float v=0; if (!json_get_float_any(&doc,t,&v) || v<0.0f || v>999.0f) { log_error("Config: %s: %s.proj_speed must be number in [0..999]", path, pfx); ok=false; } else { (dst).proj_speed=v; } } \
											if (json_object_get(&doc, t_w, "proj_radius", &t)) { float v=0; if (!json_get_float_any(&doc,t,&v) || v<0.0f || v>10.0f) { log_error("Config: %s: %s.proj_radius must be number in [0..10]", path, pfx); ok=false; } else { (dst).proj_radius=v; } } \
											if (json_object_get(&doc, t_w, "proj_life_s", &t)) { float v=0; if (!json_get_float_any(&doc,t,&v) || v<0.0f || v>60.0f) { log_error("Config: %s: %s.proj_life_s must be number in [0..60]", path, pfx); ok=false; } else { (dst).proj_life_s=v; } } \
											if (json_object_get(&doc, t_w, "proj_damage", &t)) { int v=0; if (!json_get_int(&doc,t,&v) || v<0 || v>1000000) { log_error("Config: %s: %s.proj_damage must be int in [0..1000000]", path, pfx); ok=false; } else { (dst).proj_damage=v; } } \
										} \
									} \
								} while (0)

							PARSE_WEAPON_BAL(next.weapons.handgun, "handgun");
							PARSE_WEAPON_BAL(next.weapons.shotgun, "shotgun");
							PARSE_WEAPON_BAL(next.weapons.rifle, "rifle");
							PARSE_WEAPON_BAL(next.weapons.smg, "smg");
							PARSE_WEAPON_BAL(next.weapons.rocket, "rocket");

							#undef PARSE_WEAPON_BAL
						}
					}

					// weapons.view
					int t_view = -1;
					if (json_object_get(&doc, t_weap, "view", &t_view)) {
						if (!json_token_is_object(&doc, t_view)) {
							log_error("Config: %s: weapons.view must be an object", path);
							ok = false;
						} else {
							static const char* const allowed_view[] = {"shoot_anim_fps", "shoot_anim_frames"};
							warn_unknown_keys(&doc, t_view, allowed_view, (int)(sizeof(allowed_view) / sizeof(allowed_view[0])), "weapons.view");
							int t = -1;
							if (json_object_get(&doc, t_view, "shoot_anim_fps", &t)) {
								float v = 0.0f;
								if (!json_get_float_any(&doc, t, &v) || v <= 0.0f || v > 240.0f) {
									log_error("Config: %s: weapons.view.shoot_anim_fps must be number in (0..240]", path);
									ok = false;
								} else {
									next.weapons.view.shoot_anim_fps = v;
								}
							}
							if (json_object_get(&doc, t_view, "shoot_anim_frames", &t)) {
								int v = 0;
								if (!json_get_int(&doc, t, &v) || v < 1 || v > 128) {
									log_error("Config: %s: weapons.view.shoot_anim_frames must be int in [1..128]", path);
									ok = false;
								} else {
									next.weapons.view.shoot_anim_frames = v;
								}
							}
						}
					}

					// weapons.sfx
					int t_sfx = -1;
					if (json_object_get(&doc, t_weap, "sfx", &t_sfx)) {
						if (!json_token_is_object(&doc, t_sfx)) {
							log_error("Config: %s: weapons.sfx must be an object", path);
							ok = false;
						} else {
							static const char* const allowed_sfx[] = {"handgun_shot", "shotgun_shot", "rifle_shot", "smg_shot", "rocket_shot", "shot_gain"};
							warn_unknown_keys(&doc, t_sfx, allowed_sfx, (int)(sizeof(allowed_sfx) / sizeof(allowed_sfx[0])), "weapons.sfx");
							int t = -1;
							#define PARSE_WAV(field, json_name) \
								do { \
									if (json_object_get(&doc, t_sfx, (json_name), &t)) { \
										StringView sv; \
										if (!json_get_string(&doc, t, &sv) || sv.len == 0) { \
											log_error("Config: %s: weapons.sfx.%s must be a non-empty string", path, (json_name)); \
											ok = false; \
										} else { \
											copy_sv_to_buf(next.weapons.sfx.field, sizeof(next.weapons.sfx.field), sv); \
										} \
									} \
								} while (0)
							PARSE_WAV(handgun_shot, "handgun_shot");
							PARSE_WAV(shotgun_shot, "shotgun_shot");
							PARSE_WAV(rifle_shot, "rifle_shot");
							PARSE_WAV(smg_shot, "smg_shot");
							PARSE_WAV(rocket_shot, "rocket_shot");
							#undef PARSE_WAV

							if (json_object_get(&doc, t_sfx, "shot_gain", &t)) {
								float v = 0.0f;
								if (!json_get_float_any(&doc, t, &v) || v < 0.0f || v > 1.0f) {
									log_error("Config: %s: weapons.sfx.shot_gain must be number in [0..1]", path);
									ok = false;
								} else {
									next.weapons.sfx.shot_gain = v;
								}
							}
						}
					}
				}
			}
		}

		// Asset validation (required assets must exist for the config to be accepted).
		// Validate the episode file we will attempt to load at startup.
		(void)validate_asset_file(assets, "Episodes", next.content.default_episode, path, "content.default_episode", &ok);

		if (next.audio.enabled) {
			// Validate weapon shot SFX files.
			(void)validate_asset_file(assets, "Sounds/Effects", next.weapons.sfx.handgun_shot, path, "weapons.sfx.handgun_shot", &ok);
			(void)validate_asset_file(assets, "Sounds/Effects", next.weapons.sfx.shotgun_shot, path, "weapons.sfx.shotgun_shot", &ok);
			(void)validate_asset_file(assets, "Sounds/Effects", next.weapons.sfx.rifle_shot, path, "weapons.sfx.rifle_shot", &ok);
			(void)validate_asset_file(assets, "Sounds/Effects", next.weapons.sfx.smg_shot, path, "weapons.sfx.smg_shot", &ok);
			(void)validate_asset_file(assets, "Sounds/Effects", next.weapons.sfx.rocket_shot, path, "weapons.sfx.rocket_shot", &ok);

			// Validate a representative footstep file (variant 1) if footsteps are enabled.
			if (next.footsteps.enabled && next.footsteps.variant_count > 0 && next.footsteps.filename_pattern[0] != '\0') {
				char wav[64];
				snprintf(wav, sizeof(wav), next.footsteps.filename_pattern, 1u);
				(void)validate_asset_file(assets, "Sounds/Effects", wav, path, "footsteps.filename_pattern (variant 1)", &ok);
			}
		}
	}

	json_doc_destroy(&doc);

	if (!ok) {
		if (mode == CONFIG_LOAD_STARTUP) {
			log_error("Config invalid; aborting startup");
		} else {
			log_warn("Config reload failed; keeping previous config");
		}
		return false;
	}

	// Commit atomically after full validation.
	g_cfg = next;
	if (mode == CONFIG_LOAD_RELOAD) {
		log_info("Config reloaded: %s", path);
	}
	return true;
}

static bool parse_int_strict(const char* s, int* out) {
	if (!s || !out) {
		return false;
	}
	char* end = NULL;
	long v = strtol(s, &end, 10);
	if (!end || end == s || *end != '\0') {
		return false;
	}
	if (v < (long)INT32_MIN || v > (long)INT32_MAX) {
		return false;
	}
	*out = (int)v;
	return true;
}

static bool parse_float_strict(const char* s, float* out) {
	if (!s || !out) {
		return false;
	}
	char* end = NULL;
	double v = strtod(s, &end);
	if (!end || end == s || *end != '\0') {
		return false;
	}
	if (!isfinite(v)) {
		return false;
	}
	*out = (float)v;
	return true;
}

static bool key_eq(const char* a, const char* b) {
	return a && b && strcmp(a, b) == 0;
}

static bool has_path_sep(const char* s) {
	if (!s) {
		return false;
	}
	for (const char* p = s; *p; p++) {
		if (*p == '/' || *p == '\\') {
			return true;
		}
	}
	return false;
}

static CoreConfigSetStatus type_mismatch(CoreConfigValueKind expected, CoreConfigValueKind provided, CoreConfigValueKind* out_expected) {
	if (out_expected) {
		*out_expected = expected;
	}
	(void)provided;
	return CORE_CONFIG_SET_TYPE_MISMATCH;
}

static CoreConfigSetStatus set_bool(bool* dst, const char* key_path, CoreConfigValueKind provided_kind, const char* value_str, CoreConfigValueKind* out_expected) {
	(void)key_path;
	if (provided_kind != CORE_CONFIG_VALUE_BOOL) {
		return type_mismatch(CORE_CONFIG_VALUE_BOOL, provided_kind, out_expected);
	}
	if (!value_str) {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	if (strcmp(value_str, "true") == 0) {
		*dst = true;
		return CORE_CONFIG_SET_OK;
	}
	if (strcmp(value_str, "false") == 0) {
		*dst = false;
		return CORE_CONFIG_SET_OK;
	}
	return CORE_CONFIG_SET_INVALID_VALUE;
}

static CoreConfigSetStatus set_int(int* dst, int lo, int hi, CoreConfigValueKind provided_kind, const char* value_str, CoreConfigValueKind* out_expected) {
	if (provided_kind != CORE_CONFIG_VALUE_NUMBER) {
		return type_mismatch(CORE_CONFIG_VALUE_NUMBER, provided_kind, out_expected);
	}
	int v = 0;
	if (!parse_int_strict(value_str, &v)) {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	if (v < lo || v > hi) {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	*dst = v;
	return CORE_CONFIG_SET_OK;
}

static CoreConfigSetStatus set_float(float* dst, float lo, float hi, CoreConfigValueKind provided_kind, const char* value_str, CoreConfigValueKind* out_expected) {
	if (provided_kind != CORE_CONFIG_VALUE_NUMBER) {
		return type_mismatch(CORE_CONFIG_VALUE_NUMBER, provided_kind, out_expected);
	}
	float v = 0.0f;
	if (!parse_float_strict(value_str, &v)) {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	if (v < lo || v > hi) {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	*dst = v;
	return CORE_CONFIG_SET_OK;
}

static CoreConfigSetStatus set_string(char* dst, size_t cap, CoreConfigValueKind provided_kind, const char* value_str, CoreConfigValueKind* out_expected) {
	if (provided_kind != CORE_CONFIG_VALUE_STRING) {
		return type_mismatch(CORE_CONFIG_VALUE_STRING, provided_kind, out_expected);
	}
	if (!value_str || value_str[0] == '\0') {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	strncpy(dst, value_str, cap - 1);
	dst[cap - 1] = '\0';
	return CORE_CONFIG_SET_OK;
}

static CoreConfigSetStatus set_scancode(int* dst, CoreConfigValueKind provided_kind, const char* value_str, CoreConfigValueKind* out_expected) {
	if (provided_kind != CORE_CONFIG_VALUE_STRING && provided_kind != CORE_CONFIG_VALUE_NUMBER) {
		return type_mismatch(CORE_CONFIG_VALUE_STRING, provided_kind, out_expected);
	}
	if (!value_str) {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	if (provided_kind == CORE_CONFIG_VALUE_NUMBER) {
		int sc = 0;
		if (!parse_int_strict(value_str, &sc) || sc < 0 || sc >= 512) {
			return CORE_CONFIG_SET_INVALID_VALUE;
		}
		*dst = sc;
		return CORE_CONFIG_SET_OK;
	}

	// Reuse config parser's key name support.
	JsonDoc dummy;
	memset(&dummy, 0, sizeof(dummy));
	// parse_scancode expects a JsonDoc token; we don't have one.
	// Implement minimal scancode parsing from string here.
	const char* p = value_str;
	const char* prefix = "SDL_SCANCODE_";
	if (strncmp(p, prefix, strlen(prefix)) == 0) {
		p += strlen(prefix);
	}
	char lower[64];
	{
		size_t n = strlen(p);
		if (n >= sizeof(lower)) {
			n = sizeof(lower) - 1;
		}
		for (size_t i = 0; i < n; i++) {
			lower[i] = (char)tolower((unsigned char)p[i]);
		}
		lower[n] = '\0';
	}
	if (strcmp(lower, "grave") == 0 || strcmp(lower, "backquote") == 0 || strcmp(lower, "`") == 0) {
		*dst = (int)SDL_SCANCODE_GRAVE;
		return CORE_CONFIG_SET_OK;
	}
	if (strcmp(lower, "escape") == 0 || strcmp(lower, "esc") == 0) {
		*dst = (int)SDL_SCANCODE_ESCAPE;
		return CORE_CONFIG_SET_OK;
	}
	SDL_Scancode sc = SDL_GetScancodeFromName(p);
	if (sc == SDL_SCANCODE_UNKNOWN) {
		return CORE_CONFIG_SET_INVALID_VALUE;
	}
	*dst = (int)sc;
	return CORE_CONFIG_SET_OK;
}

CoreConfigSetStatus core_config_try_set_by_path(
	const char* key_path,
	CoreConfigValueKind provided_kind,
	const char* value_str,
	CoreConfigValueKind* out_expected_kind) {
	if (!key_path || key_path[0] == '\0') {
		return CORE_CONFIG_SET_UNKNOWN_KEY;
	}
	if (out_expected_kind) {
		*out_expected_kind = CORE_CONFIG_VALUE_STRING;
	}

	// Normalize bool strings coming from console.
	char bool_norm[6];
	if (provided_kind == CORE_CONFIG_VALUE_BOOL && value_str) {
		// Expect already normalized to "true"/"false".
		(void)bool_norm;
	}

	// window
	if (key_eq(key_path, "window.title")) {
		return set_string(g_cfg.window.title, sizeof(g_cfg.window.title), provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "window.width")) {
		return set_int(&g_cfg.window.width, 320, 16384, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "window.height")) {
		return set_int(&g_cfg.window.height, 240, 16384, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "window.vsync")) {
		return set_bool(&g_cfg.window.vsync, key_path, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "window.grab_mouse")) {
		return set_bool(&g_cfg.window.grab_mouse, key_path, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "window.relative_mouse")) {
		return set_bool(&g_cfg.window.relative_mouse, key_path, provided_kind, value_str, out_expected_kind);
	}

	// render
	if (key_eq(key_path, "render.internal_width")) {
		return set_int(&g_cfg.render.internal_width, 64, 8192, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.internal_height")) {
		return set_int(&g_cfg.render.internal_height, 64, 8192, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.fov_deg")) {
		return set_float(&g_cfg.render.fov_deg, 1.0f, 179.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.point_lights_enabled")) {
		return set_bool(&g_cfg.render.point_lights_enabled, key_path, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.lighting.enabled")) {
		return set_bool(&g_cfg.render.lighting.enabled, key_path, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.lighting.fog_start")) {
		return set_float(&g_cfg.render.lighting.fog_start, 0.0f, 100000.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.lighting.fog_end")) {
		return set_float(&g_cfg.render.lighting.fog_end, 0.0f, 100000.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.lighting.ambient_scale")) {
		return set_float(&g_cfg.render.lighting.ambient_scale, 0.0f, 10.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.lighting.min_visibility")) {
		return set_float(&g_cfg.render.lighting.min_visibility, 0.0f, 10.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.lighting.quantize_steps")) {
		return set_int(&g_cfg.render.lighting.quantize_steps, 1, 256, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "render.lighting.quantize_low_cutoff")) {
		return set_float(&g_cfg.render.lighting.quantize_low_cutoff, 0.0f, 1.0f, provided_kind, value_str, out_expected_kind);
	}

	// audio
	if (key_eq(key_path, "audio.enabled")) {
		return set_bool(&g_cfg.audio.enabled, key_path, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "audio.sfx_master_volume")) {
		return set_float(&g_cfg.audio.sfx_master_volume, 0.0f, 1.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "audio.sfx_atten_min_dist")) {
		return set_float(&g_cfg.audio.sfx_atten_min_dist, 0.0f, 9999.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "audio.sfx_atten_max_dist")) {
		return set_float(&g_cfg.audio.sfx_atten_max_dist, 0.0f, 9999.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "audio.sfx_device_freq")) {
		return set_int(&g_cfg.audio.sfx_device_freq, 8000, 192000, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "audio.sfx_device_buffer_samples")) {
		return set_int(&g_cfg.audio.sfx_device_buffer_samples, 64, 65536, provided_kind, value_str, out_expected_kind);
	}

	// content
	if (key_eq(key_path, "content.default_episode")) {
		// Allow path separators? Keep consistent with existing load behavior: episode loader expects filename.
		if (provided_kind != CORE_CONFIG_VALUE_STRING) {
			return type_mismatch(CORE_CONFIG_VALUE_STRING, provided_kind, out_expected_kind);
		}
		if (!value_str || value_str[0] == '\0') {
			return CORE_CONFIG_SET_INVALID_VALUE;
		}
		strncpy(g_cfg.content.default_episode, value_str, sizeof(g_cfg.content.default_episode) - 1);
		g_cfg.content.default_episode[sizeof(g_cfg.content.default_episode) - 1] = '\0';
		return CORE_CONFIG_SET_OK;
	}

	// ui.font
	if (key_eq(key_path, "ui.font.file")) {
		if (provided_kind != CORE_CONFIG_VALUE_STRING) {
			return type_mismatch(CORE_CONFIG_VALUE_STRING, provided_kind, out_expected_kind);
		}
		if (!value_str || value_str[0] == '\0' || has_path_sep(value_str)) {
			return CORE_CONFIG_SET_INVALID_VALUE;
		}
		strncpy(g_cfg.ui.font.file, value_str, sizeof(g_cfg.ui.font.file) - 1);
		g_cfg.ui.font.file[sizeof(g_cfg.ui.font.file) - 1] = '\0';
		return CORE_CONFIG_SET_OK;
	}
	if (key_eq(key_path, "ui.font.size")) {
		return set_int(&g_cfg.ui.font.size_px, 6, 96, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "ui.font.atlas_size")) {
		return set_int(&g_cfg.ui.font.atlas_size, 128, 4096, provided_kind, value_str, out_expected_kind);
	}

	// input.bindings.*
	const char* pfx = "input.bindings.";
	if (strncmp(key_path, pfx, strlen(pfx)) == 0) {
		const char* which = key_path + strlen(pfx);
		if (which[0] == '\0') {
			return CORE_CONFIG_SET_UNKNOWN_KEY;
		}
		#define BIND2(name, field_p, field_s) \
			if (strcmp(which, (name)) == 0) { \
				CoreConfigSetStatus st = set_scancode(&g_cfg.input.field_p, provided_kind, value_str, out_expected_kind); \
				if (st == CORE_CONFIG_SET_OK) { \
					g_cfg.input.field_s = (int)SDL_SCANCODE_UNKNOWN; \
				} \
				return st; \
			}
		#define BIND1(name, field) \
			if (strcmp(which, (name)) == 0) { \
				return set_scancode(&g_cfg.input.field, provided_kind, value_str, out_expected_kind); \
			}

		BIND2("forward", forward_primary, forward_secondary);
		BIND2("back", back_primary, back_secondary);
		BIND2("left", left_primary, left_secondary);
		BIND2("right", right_primary, right_secondary);
		BIND2("dash", dash_primary, dash_secondary);
		BIND2("action", action_primary, action_secondary);
		BIND2("use", use_primary, use_secondary);
		BIND1("weapon_slot_1", weapon_slot_1);
		BIND1("weapon_slot_2", weapon_slot_2);
		BIND1("weapon_slot_3", weapon_slot_3);
		BIND1("weapon_slot_4", weapon_slot_4);
		BIND1("weapon_slot_5", weapon_slot_5);
		BIND1("weapon_prev", weapon_prev);
		BIND1("weapon_next", weapon_next);

		#undef BIND1
		#undef BIND2
		return CORE_CONFIG_SET_UNKNOWN_KEY;
	}

	// player
	if (key_eq(key_path, "player.mouse_sens_deg_per_px")) {
		return set_float(&g_cfg.player.mouse_sens_deg_per_px, 0.0f, 10.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "player.move_speed")) {
		return set_float(&g_cfg.player.move_speed, 0.0f, 100.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "player.dash_distance")) {
		return set_float(&g_cfg.player.dash_distance, 0.0f, 100.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "player.dash_cooldown_s")) {
		return set_float(&g_cfg.player.dash_cooldown_s, 0.0f, 60.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "player.weapon_view_bob_smooth_rate")) {
		return set_float(&g_cfg.player.weapon_view_bob_smooth_rate, 0.0f, 100.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "player.weapon_view_bob_phase_rate")) {
		return set_float(&g_cfg.player.weapon_view_bob_phase_rate, 0.0f, 100.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "player.weapon_view_bob_phase_base")) {
		return set_float(&g_cfg.player.weapon_view_bob_phase_base, 0.0f, 10.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "player.weapon_view_bob_phase_amp")) {
		return set_float(&g_cfg.player.weapon_view_bob_phase_amp, 0.0f, 10.0f, provided_kind, value_str, out_expected_kind);
	}

	// footsteps
	if (key_eq(key_path, "footsteps.enabled")) {
		return set_bool(&g_cfg.footsteps.enabled, key_path, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "footsteps.min_speed")) {
		return set_float(&g_cfg.footsteps.min_speed, 0.0f, 100.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "footsteps.interval_s")) {
		return set_float(&g_cfg.footsteps.interval_s, 0.0f, 60.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "footsteps.variant_count")) {
		return set_int(&g_cfg.footsteps.variant_count, 1, 999, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "footsteps.filename_pattern")) {
		CoreConfigSetStatus st = set_string(g_cfg.footsteps.filename_pattern, sizeof(g_cfg.footsteps.filename_pattern), provided_kind, value_str, out_expected_kind);
		if (st != CORE_CONFIG_SET_OK) {
			return st;
		}
		// Keep the same guidance as load-time validation.
		if (g_cfg.footsteps.filename_pattern[0] != '\0' && strstr(g_cfg.footsteps.filename_pattern, "%03") == NULL) {
			return CORE_CONFIG_SET_INVALID_VALUE;
		}
		return CORE_CONFIG_SET_OK;
	}
	if (key_eq(key_path, "footsteps.gain")) {
		return set_float(&g_cfg.footsteps.gain, 0.0f, 1.0f, provided_kind, value_str, out_expected_kind);
	}

	// weapons.view
	if (key_eq(key_path, "weapons.view.shoot_anim_fps")) {
		return set_float(&g_cfg.weapons.view.shoot_anim_fps, 0.0001f, 240.0f, provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "weapons.view.shoot_anim_frames")) {
		return set_int(&g_cfg.weapons.view.shoot_anim_frames, 1, 128, provided_kind, value_str, out_expected_kind);
	}

	// weapons.sfx
	if (key_eq(key_path, "weapons.sfx.handgun_shot")) {
		return set_string(g_cfg.weapons.sfx.handgun_shot, sizeof(g_cfg.weapons.sfx.handgun_shot), provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "weapons.sfx.shotgun_shot")) {
		return set_string(g_cfg.weapons.sfx.shotgun_shot, sizeof(g_cfg.weapons.sfx.shotgun_shot), provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "weapons.sfx.rifle_shot")) {
		return set_string(g_cfg.weapons.sfx.rifle_shot, sizeof(g_cfg.weapons.sfx.rifle_shot), provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "weapons.sfx.smg_shot")) {
		return set_string(g_cfg.weapons.sfx.smg_shot, sizeof(g_cfg.weapons.sfx.smg_shot), provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "weapons.sfx.rocket_shot")) {
		return set_string(g_cfg.weapons.sfx.rocket_shot, sizeof(g_cfg.weapons.sfx.rocket_shot), provided_kind, value_str, out_expected_kind);
	}
	if (key_eq(key_path, "weapons.sfx.shot_gain")) {
		return set_float(&g_cfg.weapons.sfx.shot_gain, 0.0f, 1.0f, provided_kind, value_str, out_expected_kind);
	}

	// weapons.balance.* (handgun/shotgun/rifle/smg/rocket)
	const char* balp = "weapons.balance.";
	if (strncmp(key_path, balp, strlen(balp)) == 0) {
		const char* rest = key_path + strlen(balp);
		WeaponBalanceConfig* dst = NULL;
		const char* field = NULL;
		if (strncmp(rest, "handgun.", 8) == 0) { dst = &g_cfg.weapons.handgun; field = rest + 8; }
		else if (strncmp(rest, "shotgun.", 8) == 0) { dst = &g_cfg.weapons.shotgun; field = rest + 8; }
		else if (strncmp(rest, "rifle.", 6) == 0) { dst = &g_cfg.weapons.rifle; field = rest + 6; }
		else if (strncmp(rest, "smg.", 4) == 0) { dst = &g_cfg.weapons.smg; field = rest + 4; }
		else if (strncmp(rest, "rocket.", 7) == 0) { dst = &g_cfg.weapons.rocket; field = rest + 7; }
		if (!dst || !field || field[0] == '\0') {
			return CORE_CONFIG_SET_UNKNOWN_KEY;
		}
		if (strcmp(field, "ammo_per_shot") == 0) {
			return set_int(&dst->ammo_per_shot, 0, 999, provided_kind, value_str, out_expected_kind);
		}
		if (strcmp(field, "shot_cooldown_s") == 0) {
			return set_float(&dst->shot_cooldown_s, 0.0f, 60.0f, provided_kind, value_str, out_expected_kind);
		}
		if (strcmp(field, "pellets") == 0) {
			return set_int(&dst->pellets, 1, 128, provided_kind, value_str, out_expected_kind);
		}
		if (strcmp(field, "spread_deg") == 0) {
			return set_float(&dst->spread_deg, 0.0f, 180.0f, provided_kind, value_str, out_expected_kind);
		}
		if (strcmp(field, "proj_speed") == 0) {
			return set_float(&dst->proj_speed, 0.0f, 999.0f, provided_kind, value_str, out_expected_kind);
		}
		if (strcmp(field, "proj_radius") == 0) {
			return set_float(&dst->proj_radius, 0.0f, 10.0f, provided_kind, value_str, out_expected_kind);
		}
		if (strcmp(field, "proj_life_s") == 0) {
			return set_float(&dst->proj_life_s, 0.0f, 60.0f, provided_kind, value_str, out_expected_kind);
		}
		if (strcmp(field, "proj_damage") == 0) {
			return set_int(&dst->proj_damage, 0, 1000000, provided_kind, value_str, out_expected_kind);
		}
		return CORE_CONFIG_SET_UNKNOWN_KEY;
	}

	return CORE_CONFIG_SET_UNKNOWN_KEY;
}
