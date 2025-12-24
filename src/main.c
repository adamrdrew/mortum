#include "core/config.h"
#include "core/game_loop.h"
#include "core/log.h"

#include "platform/fs.h"
#include "platform/input.h"
#include "platform/audio.h"
#include "platform/platform.h"
#include "platform/time.h"
#include "platform/window.h"

#include "render/draw.h"
#include "render/camera.h"
#include "render/framebuffer.h"
#include "render/present.h"
#include "render/raycast.h"
#include "render/level_mesh.h"
#include "render/texture.h"

#include "assets/asset_paths.h"
#include "assets/episode_loader.h"
#include "assets/map_loader.h"
#include "assets/midi_player.h"

#include "game/player.h"
#include "game/player_controller.h"
#include "game/game_state.h"
#include "game/hud.h"

#include "game/font.h"

#include "game/weapon_view.h"

#include "game/weapons.h"

#include "game/debug_overlay.h"
#include "game/debug_dump.h"
#include "game/perf_trace.h"
#include "game/episode_runner.h"
#include "game/purge_item.h"
#include "game/rules.h"
#include "game/sector_height.h"

#include "game/entities.h"

#include "game/sound_emitters.h"

#include <SDL.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline ColorRGBA color_from_abgr(uint32_t abgr) {
	ColorRGBA c;
	c.a = (uint8_t)((abgr >> 24) & 0xFFu);
	c.b = (uint8_t)((abgr >> 16) & 0xFFu);
	c.g = (uint8_t)((abgr >> 8) & 0xFFu);
	c.r = (uint8_t)(abgr & 0xFFu);
	return c;
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

static char* dup_cstr(const char* s) {
	if (!s) {
		return NULL;
	}
	size_t n = strlen(s);
	char* out = (char*)malloc(n + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, s, n + 1);
	return out;
}

static char* join2(const char* a, const char* b) {
	if (!a || !b) {
		return NULL;
	}
	size_t na = strlen(a);
	size_t nb = strlen(b);
	bool a_slash = (na > 0 && (a[na - 1] == '/' || a[na - 1] == '\\'));
	size_t n = na + (a_slash ? 0 : 1) + nb + 1;
	char* out = (char*)malloc(n);
	if (!out) {
		return NULL;
	}
	size_t off = 0;
	memcpy(out + off, a, na);
	off += na;
	if (!a_slash) {
		out[off++] = '/';
	}
	memcpy(out + off, b, nb);
	off += nb;
	out[off] = '\0';
	return out;
}

static char* resolve_config_path(int argc, char** argv) {
	// Precedence:
	// 1) CLI: --config <path> or CONFIG=<path>
	// 2) Env: MORTUS_CONFIG
	// 3) ~/.mortus/config.json
	// 4) ./config.json

	const char* cli_path = NULL;
	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (!a || a[0] == '\0') {
			continue;
		}
		if (strcmp(a, "--config") == 0) {
			if (i + 1 < argc) {
				cli_path = argv[i + 1];
			}
			break;
		}
		if (strncmp(a, "CONFIG=", 7) == 0) {
			cli_path = a + 7;
			break;
		}
	}
	if (cli_path && cli_path[0] != '\0') {
		return dup_cstr(cli_path);
	}

	const char* env = getenv("MORTUS_CONFIG");
	if (env && env[0] != '\0') {
		return dup_cstr(env);
	}

	const char* home = getenv("HOME");
	if (home && home[0] != '\0') {
		char* p = join2(home, ".mortus/config.json");
		if (p && file_exists(p)) {
			return p;
		}
		free(p);
	}

	if (file_exists("./config.json")) {
		return dup_cstr("./config.json");
	}

	return NULL;
}

static bool key_down2(const Input* in, int primary, int secondary) {
	return input_key_down(in, primary) || input_key_down(in, secondary);
}

static PlayerControllerInput gather_controls(const Input* in, const InputBindingsConfig* bind) {
	PlayerControllerInput ci;
	if (!bind) {
		ci.forward = input_key_down(in, SDL_SCANCODE_W) || input_key_down(in, SDL_SCANCODE_UP);
		ci.back = input_key_down(in, SDL_SCANCODE_S) || input_key_down(in, SDL_SCANCODE_DOWN);
		ci.left = input_key_down(in, SDL_SCANCODE_A);
		ci.right = input_key_down(in, SDL_SCANCODE_D);
		ci.dash = input_key_down(in, SDL_SCANCODE_LSHIFT) || input_key_down(in, SDL_SCANCODE_RSHIFT);
	} else {
		ci.forward = key_down2(in, bind->forward_primary, bind->forward_secondary);
		ci.back = key_down2(in, bind->back_primary, bind->back_secondary);
		ci.left = key_down2(in, bind->left_primary, bind->left_secondary);
		ci.right = key_down2(in, bind->right_primary, bind->right_secondary);
		ci.dash = key_down2(in, bind->dash_primary, bind->dash_secondary);
	}
	ci.mouse_dx = in->mouse_dx;
	return ci;
}

static bool gather_fire(const Input* in) {
	return (in->mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
}

int main(int argc, char** argv) {
	char prev_bgmusic[64] = "";
	char prev_soundfont[64] = "";

	bool debug_dump_enabled = false;
	const char* config_path_arg = NULL;
	const char* map_name_arg = NULL;
	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (!a || a[0] == '\0') {
			continue;
		}
		if (strcmp(a, "--config") == 0) {
			if (i + 1 < argc) {
				config_path_arg = argv[i + 1];
				i++; // consume value
			}
			continue;
		}
		if (strncmp(a, "CONFIG=", 7) == 0) {
			config_path_arg = a + 7;
			continue;
		}
		if (strcmp(a, "--debug-dump") == 0) {
			debug_dump_enabled = true;
			continue;
		}
		// Treat non-flag args as a map filename relative to Assets/Levels/.
		if (a[0] != '-') {
			map_name_arg = a;
		}
	}

	if (!log_init(LOG_LEVEL_INFO)) {
		return 1;
	}

	PlatformConfig pcfg;
	pcfg.enable_audio = true;
	if (!platform_init(&pcfg)) {
		log_shutdown();
		return 1;
	}

	FsPaths fs;
	if (!fs_paths_init(&fs, "mortum", "mortum")) {
		platform_shutdown();
		log_shutdown();
		return 1;
	}

	AssetPaths paths;
	asset_paths_init(&paths, fs.base_path);

	char* config_path = NULL;
	if (config_path_arg && config_path_arg[0] != '\0') {
		config_path = dup_cstr(config_path_arg);
	} else {
		config_path = resolve_config_path(argc, argv);
	}
	if (config_path) {
		if (!core_config_load_from_file(config_path, &paths, CONFIG_LOAD_STARTUP)) {
			free(config_path);
			asset_paths_destroy(&paths);
			fs_paths_destroy(&fs);
			platform_shutdown();
			log_shutdown();
			return 1;
		}
	} else {
		log_warn("No config file found; using built-in defaults");
	}

	const CoreConfig* cfg = core_config_get();
	bool audio_enabled = cfg->audio.enabled;

	FontSystem ui_font;
	if (!font_system_init(&ui_font, cfg->ui.font_file, cfg->ui.font_size_px, cfg->ui.font_atlas_w, cfg->ui.font_atlas_h, &paths)) {
		free(config_path);
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		platform_shutdown();
		log_shutdown();
		return 1;
	}

	// SFX core (WAV sound effects) is separate from MIDI music.
	if (!sfx_init(&paths, audio_enabled, cfg->audio.sfx_device_freq, cfg->audio.sfx_device_buffer_samples)) {
		log_warn("SFX init failed; continuing with SFX disabled");
	}
	sfx_set_master_volume(cfg->audio.sfx_master_volume);
	SoundEmitters sfx_emitters;
	sound_emitters_init(&sfx_emitters);

	EntityDefs entity_defs;
	entity_defs_init(&entity_defs);
	(void)entity_defs_load(&entity_defs, &paths);
	EntitySystem entities;
	entity_system_init(&entities, 512u);

	Window win;
	if (!window_create(&win, cfg->window.title, cfg->window.width, cfg->window.height, cfg->window.vsync)) {
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		platform_shutdown();
		log_shutdown();
		free(config_path);
		return 1;
	}

	// Capture the mouse for FPS-style turning.
	// Relative mouse mode keeps the cursor from leaving the window and provides deltas.
	SDL_SetWindowGrab(win.window, cfg->window.grab_mouse ? SDL_TRUE : SDL_FALSE);
	SDL_SetRelativeMouseMode(cfg->window.relative_mouse ? SDL_TRUE : SDL_FALSE);

	Framebuffer fb;
	if (!framebuffer_init(&fb, cfg->render.internal_width, cfg->render.internal_height)) {
		window_destroy(&win);
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		platform_shutdown();
		log_shutdown();
		free(config_path);
		return 1;
	}

	Presenter presenter;
	if (!present_init(&presenter, &win, &fb)) {
		framebuffer_destroy(&fb);
		window_destroy(&win);
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		platform_shutdown();
		log_shutdown();
		free(config_path);
		return 1;
	}

	Episode ep;
	bool ep_ok = episode_load(&ep, &paths, cfg->content.default_episode);
	MapLoadResult map;
	memset(&map, 0, sizeof(map));
	bool map_ok = false;
	const char* map_name = NULL;
	EpisodeRunner runner;
	episode_runner_init(&runner);
	bool using_episode = false;
	if (map_name_arg) {
		// A filename relative to Assets/Levels/ (e.g. "mortum_test.json").
		map_name = map_name_arg;
	} else if (ep_ok && episode_runner_start(&runner, &ep)) {
		using_episode = true;
		map_name = episode_runner_current_map(&runner, &ep);
	}
	if (map_name) {
		map_ok = map_load(&map, &paths, map_name);
		if (map_ok) {
			log_info("Map loaded: entities=%d", map.entity_count);
			if (map.entities && map.entity_count > 0) {
				log_info(
					"Map entity[0]: def='%s' sector=%d pos=(%.2f,%.2f) yaw=%.1f",
					map.entities[0].def_name[0] ? map.entities[0].def_name : "(empty)",
					map.entities[0].sector,
					map.entities[0].x,
					map.entities[0].y,
					map.entities[0].yaw_deg
				);
			}
		}
		// Validate MIDI and SoundFont existence for background music
		if (map_ok && audio_enabled) {
			bool midi_exists = map.bgmusic[0] != '\0';
			bool sf_exists = map.soundfont[0] != '\0';
			if (midi_exists && sf_exists && (strcmp(map.bgmusic, prev_bgmusic) != 0 || strcmp(map.soundfont, prev_soundfont) != 0)) {
				midi_stop();
				char* midi_path = asset_path_join(&paths, "Sounds/MIDI", map.bgmusic);
				char* sf_path = asset_path_join(&paths, "Sounds/SoundFonts", map.soundfont);
				if (!midi_path || !sf_path) {
					log_warn("MIDI path allocation failed");
				} else if (!file_exists(midi_path)) {
					log_warn("MIDI file not found: %s", midi_path);
				} else if (!file_exists(sf_path)) {
					log_warn("SoundFont file not found: %s", sf_path);
				} else {
					if (midi_init(sf_path) == 0) {
						midi_play(midi_path);
						strncpy(prev_bgmusic, map.bgmusic, sizeof(prev_bgmusic));
						strncpy(prev_soundfont, map.soundfont, sizeof(prev_soundfont));
					} else {
						log_warn("Could not initialize MIDI playback");
					}
				}
				free(midi_path);
				free(sf_path);
			}
		}
	}

	LevelMesh mesh;
	level_mesh_init(&mesh);
	if (map_ok) {
		level_mesh_build(&mesh, &map.world);
	}

	TextureRegistry texreg;
	texture_registry_init(&texreg);

	Player player;
	player_init(&player);
	if (map_ok) {
		episode_runner_apply_level_start(&player, &map);
	}

	// Spawn map-authored sound emitters (e.g., ambient loops).
	if (map_ok && map.sounds && map.sound_count > 0) {
		sound_emitters_reset(&sfx_emitters);
		for (int i = 0; i < map.sound_count; i++) {
			MapSoundEmitter* ms = &map.sounds[i];
			SoundEmitterId id = sound_emitter_create(&sfx_emitters, ms->x, ms->y, ms->spatial, ms->gain);
			if (ms->loop) {
				sound_emitter_start_loop(&sfx_emitters, id, ms->sound, player.body.x, player.body.y);
			}
		}
	}

	entity_system_reset(&entities, map_ok ? &map.world : NULL, &entity_defs);
	if (map_ok && map.entities && map.entity_count > 0) {
		entity_system_spawn_map(&entities, map.entities, map.entity_count);
	}

	Input in;
	memset(&in, 0, sizeof(in));

	GameState gs;
	game_state_init(&gs);

	float* wall_depth = (float*)malloc((size_t)fb.width * sizeof(float));
	if (!wall_depth) {
		log_error("out of memory allocating depth buffer");
	}
	float* depth_pixels = (float*)malloc((size_t)fb.width * (size_t)fb.height * sizeof(float));
	if (!depth_pixels) {
		log_error("out of memory allocating per-pixel depth buffer");
	}

	GameLoop loop;
	game_loop_init(&loop, 1.0 / 60.0);

	bool running = true;
	int frames = 0;
	double fps_t0 = platform_time_seconds();
	int fps = 0;
	bool debug_overlay_enabled = false;
	bool debug_prev_down = false;
	bool dump_prev_down = false;
	bool entity_dump_prev_down = false;
	bool fps_overlay_enabled = false;
	bool fps_prev_down = false;
	bool font_test_enabled = false;
	bool font_test_prev_down = false;
	bool perf_prev_down = false;
	PerfTrace perf;
	perf_trace_init(&perf);
	bool lights_prev_down = false;
	bool point_lights_enabled = cfg->render.point_lights_enabled;
	raycast_set_point_lights_enabled(point_lights_enabled);
	bool q_prev_down = false;
	bool e_prev_down = false;
	bool win_prev = false;
	bool reload_prev_down = false;

	while (running) {
		double frame_t0 = platform_time_seconds();
		double now = frame_t0;
		double update_t0 = 0.0, update_t1 = 0.0;
		double render3d_t0 = 0.0, render3d_t1 = 0.0;
		double ui_t0 = 0.0, ui_t1 = 0.0;
		double present_t0 = 0.0, present_t1 = 0.0;
		int steps = game_loop_begin_frame(&loop, now);

		input_begin_frame(&in);
		input_poll(&in);
		if (in.quit_requested || input_key_down(&in, SDL_SCANCODE_ESCAPE)) {
			running = false;
			if (audio_enabled) {
				midi_stop(); // Stop music on quit
			}
		}

		// Config reload (default U; rebindable).
		bool reload_down = input_key_down(&in, cfg->input.reload_config_scancode);
		bool reload_pressed = reload_down && !reload_prev_down;
		reload_prev_down = reload_down;
		if (reload_pressed) {
			char* reload_path = config_path ? dup_cstr(config_path) : resolve_config_path(argc, argv);
			if (!reload_path) {
				log_warn("Config reload requested but no config path found");
			} else {
				if (core_config_load_from_file(reload_path, &paths, CONFIG_LOAD_RELOAD)) {
					cfg = core_config_get();
					audio_enabled = cfg->audio.enabled;
					sfx_set_master_volume(cfg->audio.sfx_master_volume);
					SDL_SetWindowGrab(win.window, cfg->window.grab_mouse ? SDL_TRUE : SDL_FALSE);
					SDL_SetRelativeMouseMode(cfg->window.relative_mouse ? SDL_TRUE : SDL_FALSE);
					raycast_set_point_lights_enabled(cfg->render.point_lights_enabled);
					point_lights_enabled = cfg->render.point_lights_enabled;
					log_warn("Some config changes are startup-only (window size, internal resolution, vsync, SFX device params)");
				}
			}
			free(reload_path);
		}

		// Dev toggles
		bool dbg_down = input_key_down(&in, cfg->input.toggle_debug_overlay);
		bool dbg_pressed = dbg_down && !debug_prev_down;
		debug_prev_down = dbg_down;
		if (dbg_pressed) {
			debug_overlay_enabled = !debug_overlay_enabled;
		}

		// FPS overlay toggle (upper-right).
		bool fps_down = input_key_down(&in, cfg->input.toggle_fps_overlay);
		bool fps_pressed = fps_down && !fps_prev_down;
		fps_prev_down = fps_down;
		if (fps_pressed) {
			fps_overlay_enabled = !fps_overlay_enabled;
		}

		// Font smoke-test page toggle (F7).
		bool ft_down = input_key_down(&in, SDL_SCANCODE_F7);
		bool ft_pressed = ft_down && !font_test_prev_down;
		font_test_prev_down = ft_down;
		if (ft_pressed) {
			font_test_enabled = !font_test_enabled;
		}

		// Toggle point-light emitters (debug). This removes the emitter processing code path.
		bool lights_down = input_key_down(&in, cfg->input.toggle_point_lights);
		bool lights_pressed = lights_down && !lights_prev_down;
		lights_prev_down = lights_down;
		if (lights_pressed) {
			point_lights_enabled = !point_lights_enabled;
			raycast_set_point_lights_enabled(point_lights_enabled);
		}

		// Entity dump: press L (default) to print entity + projection diagnostics.
		bool entdump_down = input_key_down(&in, cfg->input.entity_dump);
		bool entdump_pressed = entdump_down && !entity_dump_prev_down;
		entity_dump_prev_down = entdump_down;
		if (entdump_pressed) {
			Camera cam = camera_make(player.body.x, player.body.y, player.angle_deg, cfg->render.fov_deg);
			{
				float phase = player.weapon_view_bob_phase;
				float amp = player.weapon_view_bob_amp;
				float bob_amp = amp * amp;
				float ang = player.angle_deg * (float)M_PI / 180.0f;
				float fx = cosf(ang);
				float fy = sinf(ang);
				float rx = -fy;
				float ry = fx;
				float bob_side = sinf(phase) * bob_amp * 0.03f;
				float bob_z = sinf(phase) * bob_amp * 0.006f;
				cam.x += rx * bob_side;
				cam.y += ry * bob_side;
				float floor_z = 0.0f;
				if (map_ok && (unsigned)player.body.sector < (unsigned)map.world.sector_count) {
					floor_z = map.world.sectors[player.body.sector].floor_z;
				}
				cam.z = (player.body.z - floor_z) + bob_z;
			}
			debug_dump_print_entities(stdout, map_name, map_ok ? &map.world : NULL, &player, &cam, &entities, fb.width, fb.height, wall_depth);
		}

		// Performance trace capture: press O to gather 60 frames then dump a summary.
		bool perf_down = input_key_down(&in, cfg->input.perf_trace);
		bool perf_pressed = perf_down && !perf_prev_down;
		perf_prev_down = perf_down;
		if (perf_pressed) {
			perf_trace_start(&perf, map_name, fb.width, fb.height);
			fprintf(stdout, "\n(perf trace) capturing %d frames...\n", PERF_TRACE_FRAME_COUNT);
			fflush(stdout);
		}

		// Debug dump: only when enabled via CLI and user presses `~` (grave).
		bool dump_down = input_key_down(&in, cfg->input.debug_dump);
		bool dump_pressed = dump_down && !dump_prev_down;
		dump_prev_down = dump_down;
		if (debug_dump_enabled && dump_pressed) {
			Camera cam = camera_make(player.body.x, player.body.y, player.angle_deg, cfg->render.fov_deg);
			{
				float phase = player.weapon_view_bob_phase;
				float amp = player.weapon_view_bob_amp;
				float bob_amp = amp * amp;
				float ang = player.angle_deg * (float)M_PI / 180.0f;
				float fx = cosf(ang);
				float fy = sinf(ang);
				float rx = -fy;
				float ry = fx;
				float bob_side = sinf(phase) * bob_amp * 0.03f;
				float bob_z = sinf(phase) * bob_amp * 0.006f;
				cam.x += rx * bob_side;
				cam.y += ry * bob_side;
				float floor_z = 0.0f;
				if (map_ok && (unsigned)player.body.sector < (unsigned)map.world.sector_count) {
					floor_z = map.world.sectors[player.body.sector].floor_z;
				}
				cam.z = (player.body.z - floor_z) + bob_z;
			}
			debug_dump_print(stdout, map_name, map_ok ? &map.world : NULL, &player, &cam);
		}

		bool noclip_down = input_key_down(&in, cfg->input.noclip);
		bool noclip_pressed = noclip_down && !player.noclip_prev_down;
		player.noclip_prev_down = noclip_down;
		if (noclip_pressed) {
			player.noclip = !player.noclip;
		}

		PlayerControllerInput ci = gather_controls(&in, &cfg->input);
		bool fire_down = gather_fire(&in);
		uint8_t weapon_select_mask = 0;
		if (input_key_down(&in, cfg->input.weapon_slot_1)) {
			weapon_select_mask |= 1u << 0;
		}
		if (input_key_down(&in, cfg->input.weapon_slot_2)) {
			weapon_select_mask |= 1u << 1;
		}
		if (input_key_down(&in, cfg->input.weapon_slot_3)) {
			weapon_select_mask |= 1u << 2;
		}
		if (input_key_down(&in, cfg->input.weapon_slot_4)) {
			weapon_select_mask |= 1u << 3;
		}
		if (input_key_down(&in, cfg->input.weapon_slot_5)) {
			weapon_select_mask |= 1u << 4;
		}
		int weapon_wheel_delta = in.mouse_wheel;
		bool q_down = input_key_down(&in, cfg->input.weapon_prev);
		bool e_down = input_key_down(&in, cfg->input.weapon_next);
		bool q_pressed = q_down && !q_prev_down;
		bool e_pressed = e_down && !e_prev_down;
		q_prev_down = q_down;
		e_prev_down = e_down;
		if (q_pressed) {
			weapon_wheel_delta -= 1;
		}
		if (e_pressed) {
			weapon_wheel_delta += 1;
		}
		if (perf_trace_is_active(&perf)) {
			update_t0 = platform_time_seconds();
		}
		for (int i = 0; i < steps; i++) {
			if (gs.mode == GAME_MODE_PLAYING) {
				bool action_down = key_down2(&in, cfg->input.action_primary, cfg->input.action_secondary);
				bool action_pressed = action_down && !player.action_prev_down;
				player.action_prev_down = action_down;
				if (action_pressed) {
					(void)sector_height_try_toggle_touching_wall(map_ok ? &map.world : NULL, &player, &sfx_emitters, player.body.x, player.body.y);
				}
				sector_height_update(map_ok ? &map.world : NULL, &player, &sfx_emitters, player.body.x, player.body.y, loop.fixed_dt_s);

				player_controller_update(&player, map_ok ? &map.world : NULL, &ci, loop.fixed_dt_s);
				entity_system_resolve_player_collisions(&entities, &player.body);

				entity_system_tick(&entities, &player.body, (float)loop.fixed_dt_s);
				{
					uint32_t ei = 0u;
					for (;;) {
						uint32_t ev_count = 0u;
						const EntityEvent* evs = entity_system_events(&entities, &ev_count);
						if (ei >= ev_count) {
							break;
						}
						const EntityEvent* ev = &evs[ei++];
						switch (ev->type) {
							case ENTITY_EVENT_PLAYER_TOUCH: {
								if (ev->kind != ENTITY_KIND_PICKUP) {
									break;
								}
								const EntityDef* def = &entity_defs.defs[ev->def_id];
								if (def->u.pickup.type == PICKUP_TYPE_HEALTH) {
									int after = player.health + def->u.pickup.heal_amount;
									if (after > player.health_max) {
										after = player.health_max;
									}
									if (after < 0) {
										after = 0;
									}
									player.health = after;
								} else if (def->u.pickup.type == PICKUP_TYPE_AMMO) {
									(void)ammo_add(&player.ammo, def->u.pickup.ammo_type, def->u.pickup.ammo_amount);
								}

								// Pickups are consumed on touch (even if already full).
								if (def->u.pickup.pickup_sound[0] != '\0') {
									sound_emitters_play_one_shot_at(
										&sfx_emitters,
										def->u.pickup.pickup_sound,
										ev->x,
										ev->y,
										true,
										def->u.pickup.pickup_sound_gain,
										player.body.x,
										player.body.y
									);
								}
								entity_system_request_despawn(&entities, ev->entity);
							} break;

							case ENTITY_EVENT_PROJECTILE_HIT_WALL: {
								if (ev->kind != ENTITY_KIND_PROJECTILE) {
									break;
								}
								const EntityDef* def = &entity_defs.defs[ev->def_id];
								if (def->u.projectile.impact_sound[0] != '\0') {
									sound_emitters_play_one_shot_at(
										&sfx_emitters,
										def->u.projectile.impact_sound,
										ev->x,
										ev->y,
										true,
										def->u.projectile.impact_sound_gain,
										player.body.x,
										player.body.y
									);
								}
								// Despawn already requested by entity tick, but request again is harmless.
								entity_system_request_despawn(&entities, ev->entity);
							} break;

							case ENTITY_EVENT_DAMAGE: {
								// If a projectile dealt damage, reuse its impact sound at the hit location.
								if (ev->kind == ENTITY_KIND_PROJECTILE) {
									const EntityDef* def = &entity_defs.defs[ev->def_id];
									if (def->u.projectile.impact_sound[0] != '\0') {
										sound_emitters_play_one_shot_at(
											&sfx_emitters,
											def->u.projectile.impact_sound,
											ev->x,
											ev->y,
											true,
											def->u.projectile.impact_sound_gain,
											player.body.x,
											player.body.y
										);
									}
									entity_system_request_despawn(&entities, ev->entity);
								}

								// Apply damage to target entity.
								Entity* target = NULL;
								if (entity_system_resolve(&entities, ev->other, &target) && ev->amount > 0) {
									const EntityDef* tdef = &entity_defs.defs[target->def_id];
									target->hp -= ev->amount;
									if (target->hp <= 0) {
										target->hp = 0;
										EntityEvent died;
										memset(&died, 0, sizeof(died));
										died.type = ENTITY_EVENT_DIED;
										died.entity = target->id;
										died.other = ev->entity; // source
										died.def_id = target->def_id;
										died.kind = tdef->kind;
										died.x = target->body.x;
										died.y = target->body.y;
										died.amount = 0;
										(void)entity_system_emit_event(&entities, died);
										if (tdef->kind == ENTITY_KIND_ENEMY) {
											target->state = ENTITY_STATE_DYING;
											target->state_time = 0.0f;
										} else {
											entity_system_request_despawn(&entities, target->id);
										}
									} else {
										if (tdef->kind == ENTITY_KIND_ENEMY) {
											// Taking damage triggers a brief DAMAGED reaction, then the enemy will re-engage.
											target->state = ENTITY_STATE_DAMAGED;
											target->state_time = 0.0f;
											target->attack_has_hit = false;
										}
									}
								}
							} break;

							case ENTITY_EVENT_DIED: {
								// Reserved for future: death sounds, drops, score, etc.
							} break;

							case ENTITY_EVENT_PLAYER_DAMAGE: {
								if (ev->amount > 0) {
									player.health -= ev->amount;
									if (player.health < 0) {
										player.health = 0;
									}
								}
							} break;

							default:
								break;
						}
					}
				}
				entity_system_flush(&entities);

				// Basic footsteps: emitted from player/camera position (non-spatial).
				{
					float vx = player.body.vx;
					float vy = player.body.vy;
					float speed = sqrtf(vx * vx + vy * vy);
					bool moving = cfg->footsteps.enabled && (player.body.on_ground && speed > cfg->footsteps.min_speed);
					if (moving) {
						player.footstep_timer_s -= (float)loop.fixed_dt_s;
						if (player.footstep_timer_s <= 0.0f) {
							int variants = cfg->footsteps.variant_count;
							if (variants < 1) {
								variants = 1;
							}
							player.footstep_variant = (uint8_t)((player.footstep_variant % (uint8_t)variants) + 1u);
							char wav[64];
							snprintf(wav, sizeof(wav), cfg->footsteps.filename_pattern, (unsigned)player.footstep_variant);
							sound_emitters_play_one_shot_at(&sfx_emitters, wav, player.body.x, player.body.y, false, cfg->footsteps.gain, player.body.x, player.body.y);
							player.footstep_timer_s = cfg->footsteps.interval_s;
						}
					} else {
						player.footstep_timer_s = 0.0f;
					}
				}

				weapons_update(&player, map_ok ? &map.world : NULL, &sfx_emitters, &entities, player.body.x, player.body.y, fire_down, weapon_wheel_delta, weapon_select_mask, loop.fixed_dt_s);
				bool use_down = key_down2(&in, cfg->input.use_primary, cfg->input.use_secondary);
				bool use_pressed = use_down && !player.use_prev_down;
				player.use_prev_down = use_down;
				if (use_pressed) {
					(void)purge_item_use(&player);
				}
				if (player.health <= 0) {
					gs.mode = GAME_MODE_LOSE;
				}
			}
		}
		if (perf_trace_is_active(&perf)) {
			update_t1 = platform_time_seconds();
		}

		// Episode progression on win edge.
		bool win_now = (gs.mode == GAME_MODE_WIN);
		if (win_now && !win_prev && using_episode) {
			if (episode_runner_advance(&runner, &ep)) {
				const char* next_map = episode_runner_current_map(&runner, &ep);
				if (next_map) {
					if (map_ok) {
						map_load_result_destroy(&map);
						level_mesh_destroy(&mesh);
						level_mesh_init(&mesh);
					}
					map_ok = map_load(&map, &paths, next_map);
					if (map_ok) {
						level_mesh_build(&mesh, &map.world);
						episode_runner_apply_level_start(&player, &map);
						player.footstep_timer_s = 0.0f;
						sound_emitters_reset(&sfx_emitters);
							entity_system_reset(&entities, &map.world, &entity_defs);
						if (map.sounds && map.sound_count > 0) {
							for (int i = 0; i < map.sound_count; i++) {
								MapSoundEmitter* ms = &map.sounds[i];
								SoundEmitterId id = sound_emitter_create(&sfx_emitters, ms->x, ms->y, ms->spatial, ms->gain);
								if (ms->loop) {
									sound_emitter_start_loop(&sfx_emitters, id, ms->sound, player.body.x, player.body.y);
								}
							}
						}
							if (map.entities && map.entity_count > 0) {
								entity_system_spawn_map(&entities, map.entities, map.entity_count);
							}
						gs.mode = GAME_MODE_PLAYING;
						// --- MUSIC CHANGE LOGIC ---
						if (audio_enabled) {
							bool midi_exists = map.bgmusic[0] != '\0';
							bool sf_exists = map.soundfont[0] != '\0';
							if (midi_exists && sf_exists && (strcmp(map.bgmusic, prev_bgmusic) != 0 || strcmp(map.soundfont, prev_soundfont) != 0)) {
								midi_stop();
								char* midi_path = asset_path_join(&paths, "Sounds/MIDI", map.bgmusic);
								char* sf_path = asset_path_join(&paths, "Sounds/SoundFonts", map.soundfont);
								if (!midi_path || !sf_path) {
									log_warn("MIDI path allocation failed");
								} else if (!file_exists(midi_path)) {
									log_warn("MIDI file not found: %s", midi_path);
								} else if (!file_exists(sf_path)) {
									log_warn("SoundFont file not found: %s", sf_path);
								} else {
									if (midi_init(sf_path) == 0) {
										midi_play(midi_path);
										strncpy(prev_bgmusic, map.bgmusic, sizeof(prev_bgmusic));
										strncpy(prev_soundfont, map.soundfont, sizeof(prev_soundfont));
									} else {
										log_warn("Could not initialize MIDI playback");
									}
								}
								free(midi_path);
								free(sf_path);
							}
						}
					}
				}
			}
		}
		win_prev = win_now;

		Camera cam = camera_make(player.body.x, player.body.y, player.angle_deg, cfg->render.fov_deg);
		{
			float phase = player.weapon_view_bob_phase;
			float amp = player.weapon_view_bob_amp;
			float bob_amp = amp * amp;
			float ang = player.angle_deg * (float)M_PI / 180.0f;
			float fx = cosf(ang);
			float fy = sinf(ang);
			float rx = -fy;
			float ry = fx;
			float bob_side = sinf(phase) * bob_amp * 0.03f;
			float bob_z = sinf(phase) * bob_amp * 0.006f;
			cam.x += rx * bob_side;
			cam.y += ry * bob_side;
			float floor_z = 0.0f;
			if (map_ok && (unsigned)player.body.sector < (unsigned)map.world.sector_count) {
				floor_z = map.world.sectors[player.body.sector].floor_z;
			}
			cam.z = (player.body.z - floor_z) + bob_z;
		}

		// Update looping ambient emitters with current listener position.
		sound_emitters_update(&sfx_emitters, cam.x, cam.y);
		int start_sector = -1;
		if (map_ok && (unsigned)player.body.sector < (unsigned)map.world.sector_count) {
			start_sector = player.body.sector;
		}
		RaycastPerf rc_perf;
		RaycastPerf* rc_perf_ptr = NULL;
		if (perf_trace_is_active(&perf)) {
			render3d_t0 = platform_time_seconds();
			rc_perf_ptr = &rc_perf;
		}
		raycast_render_textured_from_sector_profiled(
			&fb,
			map_ok ? &map.world : NULL,
			&cam,
			&texreg,
			&paths,
			map_ok ? map.sky : NULL,
			wall_depth,
			depth_pixels,
			start_sector,
			rc_perf_ptr
		);
		if (map_ok) {
			entity_system_draw_sprites(&entities, &fb, &map.world, &cam, start_sector, &texreg, &paths, wall_depth, depth_pixels);
		}
		if (perf_trace_is_active(&perf)) {
			render3d_t1 = platform_time_seconds();
			ui_t0 = render3d_t1;
		}

		weapon_view_draw(&fb, &player, &texreg, &paths);
		hud_draw(&ui_font, &fb, &player, &gs, fps, &texreg, &paths);
		if (debug_overlay_enabled) {
			debug_overlay_draw(&ui_font, &fb, &player, map_ok ? &map.world : NULL, &entities, fps);
		}
		if (font_test_enabled) {
			font_draw_test_page(&ui_font, &fb, 16, 16);
		}
		if (fps_overlay_enabled) {
			char fps_text[32];
			snprintf(fps_text, sizeof(fps_text), "FPS: %d", fps);
			int w = font_measure_text_width(&ui_font, fps_text, 1.0f);
			int x = fb.width - 8 - w;
			int y = 8;
			if (x < 0) {
				x = 0;
			}
			font_draw_text(&ui_font, &fb, x, y, fps_text, color_from_abgr(0xFFFFFFFFu), 1.0f);
		}
		if (perf_trace_is_active(&perf)) {
			ui_t1 = platform_time_seconds();
			present_t0 = ui_t1;
		}

		present_frame(&presenter, &win, &fb);
		if (perf_trace_is_active(&perf)) {
			present_t1 = platform_time_seconds();
			double frame_t1 = present_t1;
			PerfTraceFrame pf;
			pf.frame_ms = (frame_t1 - frame_t0) * 1000.0;
			pf.update_ms = (update_t1 - update_t0) * 1000.0;
			pf.render3d_ms = (render3d_t1 - render3d_t0) * 1000.0;
			pf.ui_ms = (ui_t1 - ui_t0) * 1000.0;
			pf.present_ms = (present_t1 - present_t0) * 1000.0;
			pf.steps = steps;
			pf.rc_planes_ms = rc_perf.planes_ms;
			pf.rc_hit_test_ms = rc_perf.hit_test_ms;
			pf.rc_walls_ms = rc_perf.walls_ms;
			pf.rc_tex_lookup_ms = rc_perf.tex_lookup_ms;
			pf.rc_light_cull_ms = rc_perf.light_cull_ms;
			pf.rc_texture_get_calls = (int)rc_perf.texture_get_calls;
			pf.rc_registry_compares = (int)rc_perf.registry_string_compares;
			pf.rc_portal_calls = (int)rc_perf.portal_calls;
			pf.rc_portal_max_depth = (int)rc_perf.portal_max_depth;
			pf.rc_wall_ray_tests = (int)rc_perf.wall_ray_tests;
			pf.rc_pixels_floor = (int)rc_perf.pixels_floor;
			pf.rc_pixels_ceil = (int)rc_perf.pixels_ceil;
			pf.rc_pixels_wall = (int)rc_perf.pixels_wall;
			pf.rc_lights_in_world = (int)rc_perf.lights_in_world;
			pf.rc_lights_visible_uncapped = (int)rc_perf.lights_visible_uncapped;
			pf.rc_lights_visible_walls = (int)rc_perf.lights_visible_walls;
			pf.rc_lights_visible_planes = (int)rc_perf.lights_visible_planes;
			pf.rc_lighting_apply_calls = (int)rc_perf.lighting_apply_calls;
			pf.rc_lighting_mul_calls = (int)rc_perf.lighting_mul_calls;
			pf.rc_lighting_apply_light_iters = (int)rc_perf.lighting_apply_light_iters;
			pf.rc_lighting_mul_light_iters = (int)rc_perf.lighting_mul_light_iters;
			perf_trace_record_frame(&perf, &pf, stdout);
		}

		frames++;
		if (now - fps_t0 >= 1.0) {
			fps = frames;
			frames = 0;
			fps_t0 = now;
		}
	}

	if (map_ok) {
		map_load_result_destroy(&map);
	}
	if (ep_ok) {
		episode_destroy(&ep);
	}

	entity_system_shutdown(&entities);
	entity_defs_destroy(&entity_defs);

	texture_registry_destroy(&texreg);
	level_mesh_destroy(&mesh);
	free(wall_depth);
	free(depth_pixels);

	present_shutdown(&presenter);
	framebuffer_destroy(&fb);
	window_destroy(&win);
	asset_paths_destroy(&paths);
	fs_paths_destroy(&fs);
	font_system_shutdown(&ui_font);
	sound_emitters_shutdown(&sfx_emitters);
	sfx_shutdown();
	midi_shutdown(); // Clean up music resources
	free(config_path);
	platform_shutdown();
	log_shutdown();
	return 0;
}
