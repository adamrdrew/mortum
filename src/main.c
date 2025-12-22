#include "core/config.h"
#include "core/game_loop.h"
#include "core/log.h"

#include "platform/fs.h"
#include "platform/input.h"
#include "platform/platform.h"
#include "platform/time.h"
#include "platform/window.h"

#include "render/draw.h"
#include "render/camera.h"
#include "render/framebuffer.h"
#include "render/font.h"
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

#include "game/weapon_view.h"

#include "game/weapons.h"

#include "game/debug_overlay.h"
#include "game/debug_dump.h"
#include "game/episode_runner.h"
#include "game/purge_item.h"
#include "game/rules.h"

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static PlayerControllerInput gather_controls(const Input* in) {
	PlayerControllerInput ci;
	ci.forward = input_key_down(in, SDL_SCANCODE_W) || input_key_down(in, SDL_SCANCODE_UP);
	ci.back = input_key_down(in, SDL_SCANCODE_S) || input_key_down(in, SDL_SCANCODE_DOWN);
	ci.left = input_key_down(in, SDL_SCANCODE_A);
	ci.right = input_key_down(in, SDL_SCANCODE_D);
	ci.dash = input_key_down(in, SDL_SCANCODE_LSHIFT) || input_key_down(in, SDL_SCANCODE_RSHIFT);
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
	const char* map_name_arg = NULL;
	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (!a || a[0] == '\0') {
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

	Window win;
	if (!window_create(&win, "Mortum", 1280, 720)) {
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		platform_shutdown();
		log_shutdown();
		return 1;
	}

	// Capture the mouse for FPS-style turning.
	// Relative mouse mode keeps the cursor from leaving the window and provides deltas.
	SDL_SetWindowGrab(win.window, SDL_TRUE);
	SDL_SetRelativeMouseMode(SDL_TRUE);

	const CoreConfig* cfg = core_config_get();
	Framebuffer fb;
	if (!framebuffer_init(&fb, cfg->internal_width, cfg->internal_height)) {
		window_destroy(&win);
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		platform_shutdown();
		log_shutdown();
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
		return 1;
	}

	Episode ep;
	bool ep_ok = episode_load(&ep, &paths, "episode1.json");
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
		// Validate MIDI and SoundFont existence for background music
		if (map_ok) {
			char midi_path[128], sf_path[128];
			get_midi_path(map.bgmusic, midi_path, sizeof(midi_path));
			get_soundfont_path(map.soundfont, sf_path, sizeof(sf_path));
			bool midi_exists = map.bgmusic[0] != '\0';
			bool sf_exists = map.soundfont[0] != '\0';
			if (midi_exists && sf_exists && (strcmp(map.bgmusic, prev_bgmusic) != 0 || strcmp(map.soundfont, prev_soundfont) != 0)) {
				midi_stop();
				FILE* mf = fopen(midi_path, "rb");
				FILE* sf = fopen(sf_path, "rb");
				if (!mf) {
					fprintf(stderr, "Warning: MIDI file not found: %s\n", midi_path);
				} else if (!sf) {
					fprintf(stderr, "Warning: SoundFont file not found: %s\n", sf_path);
					fclose(mf);
				} else {
					fclose(mf);
					fclose(sf);
					if (midi_init(sf_path) == 0) {
						midi_play(midi_path);
						strncpy(prev_bgmusic, map.bgmusic, sizeof(prev_bgmusic));
						strncpy(prev_soundfont, map.soundfont, sizeof(prev_soundfont));
					} else {
						fprintf(stderr, "Error: Could not initialize MIDI playback.\n");
					}
				}
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

	Input in;
	memset(&in, 0, sizeof(in));

	GameState gs;
	game_state_init(&gs);

	float* wall_depth = (float*)malloc((size_t)fb.width * sizeof(float));
	if (!wall_depth) {
		log_error("out of memory allocating depth buffer");
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
	bool q_prev_down = false;
	bool e_prev_down = false;
	bool win_prev = false;

	while (running) {
		double now = platform_time_seconds();
		int steps = game_loop_begin_frame(&loop, now);

		input_begin_frame(&in);
		input_poll(&in);
		if (in.quit_requested || input_key_down(&in, SDL_SCANCODE_ESCAPE)) {
			running = false;
			midi_stop(); // Stop music on quit
		}

		// Dev toggles
		bool dbg_down = input_key_down(&in, SDL_SCANCODE_F3);
		bool dbg_pressed = dbg_down && !debug_prev_down;
		debug_prev_down = dbg_down;
		if (dbg_pressed) {
			debug_overlay_enabled = !debug_overlay_enabled;
		}

		// Debug dump: only when enabled via CLI and user presses `~` (grave).
		bool dump_down = input_key_down(&in, SDL_SCANCODE_GRAVE);
		bool dump_pressed = dump_down && !dump_prev_down;
		dump_prev_down = dump_down;
		if (debug_dump_enabled && dump_pressed) {
			Camera cam = camera_make(player.x, player.y, player.angle_deg, cfg->fov_deg);
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
				cam.z = bob_z;
			}
			debug_dump_print(stdout, map_name, map_ok ? &map.world : NULL, &player, &cam);
		}

		bool noclip_down = input_key_down(&in, SDL_SCANCODE_F2);
		bool noclip_pressed = noclip_down && !player.noclip_prev_down;
		player.noclip_prev_down = noclip_down;
		if (noclip_pressed) {
			player.noclip = !player.noclip;
		}

		PlayerControllerInput ci = gather_controls(&in);
		bool fire_down = gather_fire(&in);
		uint8_t weapon_select_mask = 0;
		if (input_key_down(&in, SDL_SCANCODE_1)) {
			weapon_select_mask |= 1u << 0;
		}
		if (input_key_down(&in, SDL_SCANCODE_2)) {
			weapon_select_mask |= 1u << 1;
		}
		if (input_key_down(&in, SDL_SCANCODE_3)) {
			weapon_select_mask |= 1u << 2;
		}
		if (input_key_down(&in, SDL_SCANCODE_4)) {
			weapon_select_mask |= 1u << 3;
		}
		if (input_key_down(&in, SDL_SCANCODE_5)) {
			weapon_select_mask |= 1u << 4;
		}
		int weapon_wheel_delta = in.mouse_wheel;
		bool q_down = input_key_down(&in, SDL_SCANCODE_Q);
		bool e_down = input_key_down(&in, SDL_SCANCODE_E);
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
		for (int i = 0; i < steps; i++) {
			if (gs.mode == GAME_MODE_PLAYING) {
				player_controller_update(&player, map_ok ? &map.world : NULL, &ci, loop.fixed_dt_s);
				weapons_update(&player, map_ok ? &map.world : NULL, fire_down, weapon_wheel_delta, weapon_select_mask, loop.fixed_dt_s);
				bool use_down = input_key_down(&in, SDL_SCANCODE_F);
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
						gs.mode = GAME_MODE_PLAYING;
						// --- MUSIC CHANGE LOGIC ---
						char midi_path[128], sf_path[128];
						get_midi_path(map.bgmusic, midi_path, sizeof(midi_path));
						get_soundfont_path(map.soundfont, sf_path, sizeof(sf_path));
						bool midi_exists = map.bgmusic[0] != '\0';
						bool sf_exists = map.soundfont[0] != '\0';
						if (midi_exists && sf_exists && (strcmp(map.bgmusic, prev_bgmusic) != 0 || strcmp(map.soundfont, prev_soundfont) != 0)) {
							midi_stop();
							FILE* mf = fopen(midi_path, "rb");
							FILE* sf = fopen(sf_path, "rb");
							if (!mf) {
								fprintf(stderr, "Warning: MIDI file not found: %s\n", midi_path);
							} else if (!sf) {
								fprintf(stderr, "Warning: SoundFont file not found: %s\n", sf_path);
								fclose(mf);
							} else {
								fclose(mf);
								fclose(sf);
								if (midi_init(sf_path) == 0) {
									midi_play(midi_path);
									strncpy(prev_bgmusic, map.bgmusic, sizeof(prev_bgmusic));
									strncpy(prev_soundfont, map.soundfont, sizeof(prev_soundfont));
								} else {
									fprintf(stderr, "Error: Could not initialize MIDI playback.\n");
								}
							}
						}
					}
				}
			}
		}
		win_prev = win_now;

		Camera cam = camera_make(player.x, player.y, player.angle_deg, cfg->fov_deg);
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
			cam.z = bob_z;
		}
		raycast_render_textured(&fb, map_ok ? &map.world : NULL, &cam, &texreg, &paths, map_ok ? map.sky : NULL, wall_depth);

		weapon_view_draw(&fb, &player, &texreg, &paths);
		hud_draw(&fb, &player, &gs, fps, &texreg, &paths);
		if (debug_overlay_enabled) {
			debug_overlay_draw(&fb, &player, map_ok ? &map.world : NULL, fps);
		}

		present_frame(&presenter, &win, &fb);

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

	texture_registry_destroy(&texreg);
	level_mesh_destroy(&mesh);
	free(wall_depth);

	present_shutdown(&presenter);
	framebuffer_destroy(&fb);
	window_destroy(&win);
	asset_paths_destroy(&paths);
	fs_paths_destroy(&fs);
	platform_shutdown();
	log_shutdown();
	midi_shutdown(); // Clean up music resources
	return 0;
}
