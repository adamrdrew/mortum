#include "game/console_commands.h"

#include "assets/midi_player.h"

#include "core/log.h"

#include "game/debug_dump.h"

#include "render/camera.h"
#include "render/raycast.h"

#include <SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// -----------------------------
// Helpers
// -----------------------------

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

static bool parse_bool_norm(const char* s, bool* out) {
	if (!s || !out) {
		return false;
	}
	if (strcmp(s, "true") == 0) {
		*out = true;
		return true;
	}
	if (strcmp(s, "false") == 0) {
		*out = false;
		return true;
	}
	return false;
}

static void lower_inplace(char* s) {
	if (!s) {
		return;
	}
	for (; *s; s++) {
		*s = (char)tolower((unsigned char)*s);
	}
}

static bool looks_like_number(const char* s) {
	if (!s || !s[0]) {
		return false;
	}
	char* end = NULL;
	(void)strtod(s, &end);
	return end && end != s && *end == '\0';
}

static CoreConfigValueKind infer_value_kind(const char* s) {
	if (!s) {
		return CORE_CONFIG_VALUE_STRING;
	}
	if (strcmp(s, "true") == 0 || strcmp(s, "false") == 0) {
		return CORE_CONFIG_VALUE_BOOL;
	}
	if (looks_like_number(s)) {
		return CORE_CONFIG_VALUE_NUMBER;
	}
	return CORE_CONFIG_VALUE_STRING;
}

static const char* kind_to_string(CoreConfigValueKind k) {
	switch (k) {
		case CORE_CONFIG_VALUE_STRING: return "string";
		case CORE_CONFIG_VALUE_BOOL: return "boolean";
		case CORE_CONFIG_VALUE_NUMBER: return "number";
		default: return "unknown";
	}
}

static bool name_is_safe_filename(const char* name) {
	if (!name || !name[0]) {
		return false;
	}
	// Disallow path separators and traversal.
	if (strstr(name, "..") != NULL) {
		return false;
	}
	for (const char* p = name; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '/' || c == '\\') {
			return false;
		}
		if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
			return false;
		}
	}
	return true;
}

static void print_memstream_lines(Console* con, void (*fn)(FILE* out, void* u), void* u) {
	char* buf = NULL;
	size_t sz = 0;
	FILE* f = open_memstream(&buf, &sz);
	if (!f) {
		console_print(con, "Error: Failed to capture output.");
		return;
	}
	fn(f, u);
	fflush(f);
	fclose(f);
	if (!buf) {
		return;
	}
	char* p = buf;
	while (*p) {
		char* e = strchr(p, '\n');
		if (e) {
			*e = '\0';
		}
		if (*p) {
			console_print(con, p);
		}
		if (!e) {
			break;
		}
		p = e + 1;
	}
	free(buf);
}

static bool cmd_set_bool(Console* con, int argc, const char** argv, bool* dst) {
	if (!dst) {
		return false;
	}
	if (argc < 1) {
		console_print(con, "Error: Expected boolean");
		return false;
	}
	char tmp[16];
	strncpy(tmp, argv[0] ? argv[0] : "", sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = '\0';
	lower_inplace(tmp);
	bool v = false;
	if (!parse_bool_norm(tmp, &v)) {
		console_print(con, "Error: Expected boolean");
		return false;
	}
	*dst = v;
	console_print(con, "OK");
	return true;
}

static void refresh_runtime_audio(ConsoleCommandContext* ctx) {
	if (!ctx || !ctx->sfx_emitters || !ctx->audio_enabled || !ctx->sound_emitters_enabled) {
		return;
	}
	sound_emitters_set_enabled(ctx->sfx_emitters, (*ctx->audio_enabled) && (*ctx->sound_emitters_enabled));
}

static void maybe_start_music_for_map(ConsoleCommandContext* ctx) {
	if (!ctx || !ctx->paths || !ctx->audio_enabled || !ctx->music_enabled || !ctx->map_ok || !ctx->map) {
		return;
	}
	if (!*ctx->audio_enabled || !*ctx->music_enabled) {
		return;
	}
	if (!*ctx->map_ok) {
		return;
	}
	const char* midi_name = ctx->map->bgmusic;
	const char* sf_name = ctx->map->soundfont;
	if (!midi_name || !sf_name || midi_name[0] == '\0' || sf_name[0] == '\0') {
		return;
	}
	if (ctx->prev_bgmusic && strncmp(ctx->prev_bgmusic, midi_name, ctx->prev_bgmusic_cap) == 0 &&
		ctx->prev_soundfont && strncmp(ctx->prev_soundfont, sf_name, ctx->prev_soundfont_cap) == 0) {
		return;
	}

	char* midi_path = asset_path_join(ctx->paths, "Sounds/MIDI", midi_name);
	char* sf_path = asset_path_join(ctx->paths, "Sounds/SoundFonts", sf_name);
	if (!midi_path || !sf_path) {
		free(midi_path);
		free(sf_path);
		return;
	}
	if (!file_exists(midi_path) || !file_exists(sf_path)) {
		free(midi_path);
		free(sf_path);
		return;
	}

	midi_stop();
	if (midi_init(sf_path) == 0) {
		midi_play(midi_path);
		if (ctx->prev_bgmusic && ctx->prev_bgmusic_cap > 0) {
			strncpy(ctx->prev_bgmusic, midi_name, ctx->prev_bgmusic_cap);
			ctx->prev_bgmusic[ctx->prev_bgmusic_cap - 1] = '\0';
		}
		if (ctx->prev_soundfont && ctx->prev_soundfont_cap > 0) {
			strncpy(ctx->prev_soundfont, sf_name, ctx->prev_soundfont_cap);
			ctx->prev_soundfont[ctx->prev_soundfont_cap - 1] = '\0';
		}
	}

	free(midi_path);
	free(sf_path);
}

static void respawn_map_emitters_and_entities(ConsoleCommandContext* ctx) {
	if (!ctx || !ctx->map_ok || !ctx->map || !*ctx->map_ok) {
		return;
	}
	// Map-authored sound emitters.
	if (ctx->sfx_emitters && ctx->player) {
		sound_emitters_reset(ctx->sfx_emitters);
		if (ctx->map->sounds && ctx->map->sound_count > 0) {
			for (int i = 0; i < ctx->map->sound_count; i++) {
				MapSoundEmitter* ms = &ctx->map->sounds[i];
				SoundEmitterId id = sound_emitter_create(ctx->sfx_emitters, ms->x, ms->y, ms->spatial, ms->gain);
				if (ms->loop) {
					sound_emitter_start_loop(ctx->sfx_emitters, id, ms->sound, ctx->player->body.x, ctx->player->body.y);
				}
			}
		}
	}

	// Entities.
	if (ctx->entities && ctx->entity_defs) {
		entity_system_reset(ctx->entities, &ctx->map->world, ctx->entity_defs);
		if (ctx->map->entities && ctx->map->entity_count > 0) {
			entity_system_spawn_map(ctx->entities, ctx->map->entities, ctx->map->entity_count);
		}
	}

	refresh_runtime_audio(ctx);
}

static bool load_map_by_name(ConsoleCommandContext* ctx, const char* map_name, bool stop_episode) {
	if (!ctx || !ctx->map || !ctx->paths || !ctx->map_ok || !ctx->mesh || !ctx->player || !ctx->gs) {
		return false;
	}
	if (!name_is_safe_filename(map_name)) {
		return false;
	}
	// map_load() overwrites the MapLoadResult struct; destroy prior owned allocations first.
	if (*ctx->map_ok) {
		map_load_result_destroy(ctx->map);
		*ctx->map_ok = false;
	}
	bool ok = map_load(ctx->map, ctx->paths, map_name);
	*ctx->map_ok = ok;
	if (!ok) {
		return false;
	}
	if (ctx->map_name_buf && ctx->map_name_cap > 0) {
		strncpy(ctx->map_name_buf, map_name, ctx->map_name_cap);
		ctx->map_name_buf[ctx->map_name_cap - 1] = '\0';
	}
	if (stop_episode && ctx->using_episode) {
		*ctx->using_episode = false;
	}

	level_mesh_build(ctx->mesh, &ctx->map->world);
	episode_runner_apply_level_start(ctx->player, ctx->map);
	ctx->player->footstep_timer_s = 0.0f;
	respawn_map_emitters_and_entities(ctx);
	ctx->gs->mode = GAME_MODE_PLAYING;
	maybe_start_music_for_map(ctx);
	return true;
}

static bool load_episode_by_name(ConsoleCommandContext* ctx, const char* episode_name) {
	if (!ctx || !ctx->ep || !ctx->runner || !name_is_safe_filename(episode_name)) {
		return false;
	}
	// episode_load() overwrites the Episode struct; destroy prior owned allocations first.
	episode_destroy(ctx->ep);
	if (!episode_load(ctx->ep, ctx->paths, episode_name)) {
		return false;
	}
	episode_runner_init(ctx->runner);
	if (!episode_runner_start(ctx->runner, ctx->ep)) {
		return false;
	}
	if (ctx->using_episode) {
		*ctx->using_episode = true;
	}
	const char* map_name = episode_runner_current_map(ctx->runner, ctx->ep);
	if (!map_name) {
		return false;
	}
	return load_map_by_name(ctx, map_name, false);
}

// -----------------------------
// Commands
// -----------------------------

static bool cmd_clear(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_exit(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_config_change(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_config_reload(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_load_map(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_load_episode(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_dump_perf(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_dump_entities(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_show_fps(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_noclip(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_show_debug(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_show_font_test(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_enable_light_emitters(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_enable_sound_emitters(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_enable_music(Console* con, int argc, const char** argv, void* user_ctx);
static bool cmd_help(Console* con, int argc, const char** argv, void* user_ctx);

static bool cmd_clear(Console* con, int argc, const char** argv, void* user_ctx) {
	(void)argc;
	(void)argv;
	(void)user_ctx;
	console_clear(con);
	return true;
}

static bool cmd_exit(Console* con, int argc, const char** argv, void* user_ctx) {
	(void)argc;
	(void)argv;
	(void)user_ctx;
	console_set_open(con, false);
	return true;
}

static bool cmd_help(Console* con, int argc, const char** argv, void* user_ctx) {
	(void)user_ctx;
	if (!con) {
		return false;
	}
	if (argc <= 0) {
		console_print(con, "Available Commands:");
		for (int i = 0; i < con->command_count; i++) {
			console_print(con, con->commands[i].name);
		}
		console_print(con, "");
		console_print(con, "Get detailed help for any command:");
		console_print(con, "help <command>");
		console_print(con, "");
		console_print(con, "Console keys:");
		console_print(con, "- Up/Down: command history");
		console_print(con, "- Shift+Up/Down: scroll console output");
		console_print(con, "- Enter: run command");
		console_print(con, "- `: open/close console");
		return true;
	}
	const char* name = argv[0];
	for (int i = 0; i < con->command_count; i++) {
		if (strcasecmp(con->commands[i].name, name) == 0) {
			const ConsoleCommand* c = &con->commands[i];
			if (c->description && c->description[0]) {
				console_print(con, c->description);
			}
			if ((c->example && c->example[0]) || (c->syntax && c->syntax[0])) {
				console_print(con, "");
			}
			if (c->example && c->example[0]) {
				console_print(con, "Example Usage:");
				console_print(con, c->example);
			}
			if (c->syntax && c->syntax[0]) {
				console_print(con, "");
				console_print(con, "Syntax:");
				console_print(con, c->syntax);
			}
			return true;
		}
	}
	console_print(con, "Error: Unknown command.");
	return false;
}

static bool cmd_config_reload(Console* con, int argc, const char** argv, void* user_ctx) {
	(void)argc;
	(void)argv;
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	if (!ctx || !ctx->paths) {
		return false;
	}

	const char* reload_path = ctx->config_path;
	if (!reload_path || reload_path[0] == '\0') {
		reload_path = "./config.json";
	}

	bool ok = core_config_load_from_file(reload_path, ctx->paths, CONFIG_LOAD_RELOAD);
	if (!ok) {
		console_print(con, "Error: Config reload failed.");
		return false;
	}
	if (ctx->cfg) {
		*ctx->cfg = core_config_get();
	}
	refresh_runtime_audio(ctx);
	if (ctx->win && ctx->cfg && *ctx->cfg) {
		SDL_SetWindowGrab(ctx->win->window, (*ctx->cfg)->window.grab_mouse ? SDL_TRUE : SDL_FALSE);
		SDL_SetRelativeMouseMode((*ctx->cfg)->window.relative_mouse ? SDL_TRUE : SDL_FALSE);
	}
	console_print(con, "OK");
	console_print(con, "Some config changes are startup-only (window size, internal resolution, vsync, SFX device params, UI font)");
	return true;
}

static bool cmd_config_change(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	if (argc < 2) {
		console_print(con, "Error: Expected <key_path> <value>");
		return false;
	}
	const char* key_path = argv[0];
	char value_raw[256];
	strncpy(value_raw, argv[1] ? argv[1] : "", sizeof(value_raw) - 1);
	value_raw[sizeof(value_raw) - 1] = '\0';

	// Preserve string case (filenames, titles) but accept case-insensitive booleans.
	char value_lower[256];
	strncpy(value_lower, value_raw, sizeof(value_lower) - 1);
	value_lower[sizeof(value_lower) - 1] = '\0';
	lower_inplace(value_lower);

	CoreConfigValueKind provided = CORE_CONFIG_VALUE_STRING;
	const char* value_to_pass = value_raw;
	if (strcmp(value_lower, "true") == 0 || strcmp(value_lower, "false") == 0) {
		provided = CORE_CONFIG_VALUE_BOOL;
		value_to_pass = value_lower; // core_config_try_set_by_path expects normalized "true"/"false".
	} else if (looks_like_number(value_raw)) {
		provided = CORE_CONFIG_VALUE_NUMBER;
		value_to_pass = value_raw;
	} else {
		provided = CORE_CONFIG_VALUE_STRING;
		value_to_pass = value_raw;
	}
	CoreConfigValueKind expected = CORE_CONFIG_VALUE_STRING;
	CoreConfigSetStatus st = core_config_try_set_by_path(key_path, provided, value_to_pass, &expected);
	if (st == CORE_CONFIG_SET_UNKNOWN_KEY) {
		console_print(con, "Error: Unknown config key");
		return false;
	}
	if (st == CORE_CONFIG_SET_TYPE_MISMATCH) {
		char buf[256];
		snprintf(buf, sizeof(buf), "Error: %s expected %s, got %s", key_path, kind_to_string(expected), kind_to_string(provided));
		console_print(con, buf);
		return false;
	}
	if (st != CORE_CONFIG_SET_OK) {
		console_print(con, "Error: Invalid value");
		return false;
	}
	refresh_runtime_audio(ctx);
	console_print(con, "OK");
	return true;
}

static bool cmd_load_map(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	if (!ctx || argc < 1) {
		console_print(con, "Error: Expected <map.json>");
		return false;
	}
	if (!load_map_by_name(ctx, argv[0], true)) {
		console_print(con, "Error: Failed to load map.");
		return false;
	}
	console_print(con, "OK");
	return true;
}

static bool cmd_load_episode(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	if (!ctx || argc < 1) {
		console_print(con, "Error: Expected <episode.json>");
		return false;
	}
	if (!load_episode_by_name(ctx, argv[0])) {
		console_print(con, "Error: Failed to load episode.");
		return false;
	}
	console_print(con, "OK");
	return true;
}

static bool cmd_dump_perf(Console* con, int argc, const char** argv, void* user_ctx) {
	(void)argc;
	(void)argv;
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	if (!ctx || !ctx->perf || !ctx->fb) {
		return false;
	}
	perf_trace_start(ctx->perf, (ctx->map_name_buf && ctx->map_name_buf[0]) ? ctx->map_name_buf : "(unknown)", ctx->fb->width, ctx->fb->height);
	console_print(con, "(perf trace) capturing 60 frames...");
	return true;
}

typedef struct DumpEntitiesCtx {
	ConsoleCommandContext* ctx;
} DumpEntitiesCtx;

static void dump_entities_to_file(FILE* out, void* u) {
	DumpEntitiesCtx* d = (DumpEntitiesCtx*)u;
	ConsoleCommandContext* ctx = d ? d->ctx : NULL;
	if (!ctx || !out || !ctx->player || !ctx->fb || !ctx->wall_depth) {
		return;
	}
	const CoreConfig* cfg = (ctx->cfg && *ctx->cfg) ? *ctx->cfg : core_config_get();
	Camera cam = camera_make(ctx->player->body.x, ctx->player->body.y, ctx->player->angle_deg, cfg ? cfg->render.fov_deg : 75.0f);
	debug_dump_print_entities(
		out,
		(ctx->map_name_buf && ctx->map_name_buf[0]) ? ctx->map_name_buf : "(unknown)",
		(ctx->map_ok && *ctx->map_ok) ? &ctx->map->world : NULL,
		ctx->player,
		&cam,
		ctx->entities,
		ctx->fb->width,
		ctx->fb->height,
		ctx->wall_depth);
}

static bool cmd_dump_entities(Console* con, int argc, const char** argv, void* user_ctx) {
	(void)argc;
	(void)argv;
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	if (!ctx || !ctx->fb || !ctx->wall_depth) {
		console_print(con, "Error: Entities dump unavailable.");
		return false;
	}
	DumpEntitiesCtx d;
	d.ctx = ctx;
	print_memstream_lines(con, dump_entities_to_file, &d);
	return true;
}

static bool cmd_show_fps(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	return cmd_set_bool(con, argc, argv, ctx ? ctx->show_fps : NULL);
}

static bool cmd_show_debug(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	return cmd_set_bool(con, argc, argv, ctx ? ctx->show_debug : NULL);
}

static bool cmd_show_font_test(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	return cmd_set_bool(con, argc, argv, ctx ? ctx->show_font_test : NULL);
}

static bool cmd_enable_light_emitters(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	bool ok = cmd_set_bool(con, argc, argv, ctx ? ctx->light_emitters_enabled : NULL);
	if (ok && ctx && ctx->light_emitters_enabled) {
		raycast_set_point_lights_enabled(*ctx->light_emitters_enabled);
	}
	return ok;
}

static bool cmd_enable_sound_emitters(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	bool ok = cmd_set_bool(con, argc, argv, ctx ? ctx->sound_emitters_enabled : NULL);
	if (ok && ctx) {
		refresh_runtime_audio(ctx);
	}
	return ok;
}

static bool cmd_enable_music(Console* con, int argc, const char** argv, void* user_ctx) {
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	bool ok = cmd_set_bool(con, argc, argv, ctx ? ctx->music_enabled : NULL);
	if (ok && ctx && ctx->music_enabled) {
		if (!*ctx->music_enabled) {
			midi_stop();
		} else {
			maybe_start_music_for_map(ctx);
		}
	}
	return ok;
}

static bool cmd_noclip(Console* con, int argc, const char** argv, void* user_ctx) {
	(void)argc;
	(void)argv;
	ConsoleCommandContext* ctx = (ConsoleCommandContext*)user_ctx;
	if (!ctx || !ctx->player) {
		return false;
	}
	ctx->player->noclip = !ctx->player->noclip;
	console_print(con, ctx->player->noclip ? "OK (noclip on)" : "OK (noclip off)");
	return true;
}

void console_commands_register_all(Console* con) {
	if (!con) {
		return;
	}
	// Keep this list in the exact order requested.
	(void)console_register_command(con, (ConsoleCommand){
		.name = "Clear",
		.description = "Clears the console output.",
		.example = "Clear",
		.syntax = "Clear",
		.fn = cmd_clear,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "exit",
		.description = "Closes the console.",
		.example = "exit",
		.syntax = "exit",
		.fn = cmd_exit,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "config_change",
		.description = "Changes a config key in memory (validated).",
		.example = "config_change audio.sfx_master_volume 0.85",
		.syntax = "config_change <key_path> <string|number|boolean>",
		.fn = cmd_config_change,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "config_reload",
		.description = "Reloads config.json from disk.",
		.example = "config_reload",
		.syntax = "config_reload",
		.fn = cmd_config_reload,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "load_map",
		.description = "Loads a map from Assets/Levels/.",
		.example = "load_map big.json",
		.syntax = "load_map string",
		.fn = cmd_load_map,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "load_episode",
		.description = "Loads an episode from Assets/Episodes/.",
		.example = "load_episode episode1.json",
		.syntax = "load_episode string",
		.fn = cmd_load_episode,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "dump_perf",
		.description = "Starts a 60-frame perf trace capture.",
		.example = "dump_perf",
		.syntax = "dump_perf",
		.fn = cmd_dump_perf,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "dump_entities",
		.description = "Prints an entity + projection dump into the console.",
		.example = "dump_entities",
		.syntax = "dump_entities",
		.fn = cmd_dump_entities,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "show_fps",
		.description = "Shows current frames per second.",
		.example = "show_fps true",
		.syntax = "show_fps boolean",
		.fn = cmd_show_fps,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "noclip",
		.description = "Toggles noclip movement for the player.",
		.example = "noclip",
		.syntax = "noclip",
		.fn = cmd_noclip,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "show_debug",
		.description = "Toggles the debug overlay.",
		.example = "show_debug true",
		.syntax = "show_debug boolean",
		.fn = cmd_show_debug,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "show_font_test",
		.description = "Toggles the font smoke test page.",
		.example = "show_font_test true",
		.syntax = "show_font_test boolean",
		.fn = cmd_show_font_test,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "enable_light_emitters",
		.description = "Enables or disables point-light emitters.",
		.example = "enable_light_emitters true",
		.syntax = "enable_light_emitters boolean",
		.fn = cmd_enable_light_emitters,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "enable_sound_emitters",
		.description = "Enables or disables sound emitters (SFX).",
		.example = "enable_sound_emitters true",
		.syntax = "enable_sound_emitters boolean",
		.fn = cmd_enable_sound_emitters,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "enable_music",
		.description = "Enables or disables background music playback.",
		.example = "enable_music true",
		.syntax = "enable_music boolean",
		.fn = cmd_enable_music,
	});
	(void)console_register_command(con, (ConsoleCommand){
		.name = "help",
		.description = "Lists commands or shows help for a specific command.",
		.example = "help load_map",
		.syntax = "help | help <command>",
		.fn = cmd_help,
	});
}
