#include "core/config.h"
#include "core/game_loop.h"
#include "core/log.h"
#include "core/crash_diag.h"

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
#include "render/vga_palette.h"
#include "render/raycast.h"
#include "render/level_mesh.h"
#include "render/texture.h"

#include "assets/asset_paths.h"
#include "assets/menu_loader.h"
#include "assets/timeline_loader.h"
#include "assets/map_loader.h"
#include "assets/map_validate.h"
#include "assets/midi_player.h"

#include "core/path_safety.h"

#include "game/player.h"
#include "game/player_controller.h"
#include "game/game_state.h"
#include "game/hud.h"

#include "game/postfx.h"

#include "game/gore.h"

#include "game/font.h"

#include "game/weapon_view.h"

#include "game/weapons.h"

#include "game/debug_overlay.h"
#include "game/debug_dump.h"
#include "game/perf_trace.h"
#include "game/level_start.h"
#include "game/timeline_flow.h"
#include "game/purge_item.h"
#include "game/rules.h"
#include "game/sector_height.h"
#include "game/map_music.h"

#include "game/doors.h"

#include "game/entities.h"

#include "game/inventory.h"

#include "game/particle_emitters.h"

#include "game/sound_emitters.h"

#include "game/console.h"
#include "game/console_commands.h"

#include "game/notifications.h"

#include "game/screen_runtime.h"
#include "assets/scene_loader.h"
#include "game/menu_screen.h"
#include "game/scene_screen.h"

#include <SDL.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

static int ci_tolower(int c) {
	if (c >= 'A' && c <= 'Z') {
		return c - 'A' + 'a';
	}
	return c;
}

static bool ends_with_ci2(const char* s, const char* suffix) {
	if (!s || !suffix) {
		return false;
	}
	size_t ns = strlen(s);
	size_t nf = strlen(suffix);
	if (nf > ns) {
		return false;
	}
	const char* tail = s + (ns - nf);
	for (size_t i = 0; i < nf; i++) {
		if (ci_tolower((unsigned char)tail[i]) != ci_tolower((unsigned char)suffix[i])) {
			return false;
		}
	}
	return true;
}

static void json_write_escaped_string(FILE* f, const char* s) {
	if (!f) {
		return;
	}
	if (!s) {
		fputs("null", f);
		return;
	}
	fputc('"', f);
	for (const unsigned char* p = (const unsigned char*)s; *p; p++) {
		unsigned char c = *p;
		switch (c) {
			case '"': fputs("\\\"", f); break;
			case '\\': fputs("\\\\", f); break;
			case '\b': fputs("\\b", f); break;
			case '\f': fputs("\\f", f); break;
			case '\n': fputs("\\n", f); break;
			case '\r': fputs("\\r", f); break;
			case '\t': fputs("\\t", f); break;
			default:
				if (c < 0x20) {
					fprintf(f, "\\u%04x", (unsigned)c);
				} else {
					fputc((int)c, f);
				}
				break;
		}
	}
	fputc('"', f);
}

static const char* strstr_last(const char* haystack, const char* needle) {
	if (!haystack || !needle || needle[0] == '\0') {
		return NULL;
	}
	const char* last = NULL;
	const char* p = haystack;
	while ((p = strstr(p, needle)) != NULL) {
		last = p;
		p = p + 1;
	}
	return last;
}

static bool get_exe_dir(char* out, size_t out_cap, const char* argv0) {
	if (!out || out_cap == 0) {
		return false;
	}
	out[0] = '\0';

	char path_buf[PATH_MAX];
	path_buf[0] = '\0';

	bool got = false;
	if (argv0 && argv0[0] != '\0') {
		char* rp = realpath(argv0, path_buf);
		if (rp) {
			got = true;
		}
	}

#if defined(__APPLE__)
	if (!got) {
		uint32_t sz = (uint32_t)sizeof(path_buf);
		if (_NSGetExecutablePath(path_buf, &sz) == 0) {
			char resolved[PATH_MAX];
			if (realpath(path_buf, resolved)) {
				(void)snprintf(path_buf, sizeof(path_buf), "%s", resolved);
				got = true;
			}
		}
	}
#endif

	if (!got || path_buf[0] == '\0') {
		return false;
	}

	char* slash = strrchr(path_buf, '/');
	if (!slash) {
		return false;
	}
	*slash = '\0';
	(void)snprintf(out, out_cap, "%s", path_buf);
	return out[0] != '\0';
}

static bool normalize_map_filename_arg(const char* input, char* out, size_t out_cap) {
	if (!input || input[0] == '\0' || !out || out_cap == 0) {
		return false;
	}
	const char* tail = NULL;
	const char* m1 = strstr_last(input, "Assets/Levels/");
	const char* m2 = strstr_last(input, "Assets\\\\Levels\\\\");
	if (m1) {
		tail = m1 + strlen("Assets/Levels/");
	} else if (m2) {
		tail = m2 + strlen("Assets\\\\Levels\\\\");
	} else {
		tail = input;
	}
	if (!tail || tail[0] == '\0') {
		return false;
	}
	if (!name_is_safe_relpath(tail) || !ends_with_ci2(tail, ".json")) {
		return false;
	}
	(void)snprintf(out, out_cap, "%s", tail);
	return true;
}

static void report_push_entry(MapValidationReport* r, bool is_error, const char* code, const char* message, MapValidationContext ctx) {
	if (!r || !code || !message) {
		return;
	}
	size_t n = strlen(message);
	char* msg = (char*)malloc(n + 1);
	if (!msg) {
		return;
	}
	memcpy(msg, message, n + 1);

	MapValidationEntry e;
	e.code = code;
	e.message = msg;
	e.context = ctx;

	if (is_error) {
		if (r->error_count >= r->error_cap) {
			int next_cap = (r->error_cap == 0) ? 8 : (r->error_cap * 2);
			MapValidationEntry* next = (MapValidationEntry*)realloc(r->errors, (size_t)next_cap * sizeof(*next));
			if (!next) {
				free(msg);
				return;
			}
			r->errors = next;
			r->error_cap = next_cap;
		}
		r->errors[r->error_count++] = e;
	} else {
		if (r->warning_count >= r->warning_cap) {
			int next_cap = (r->warning_cap == 0) ? 8 : (r->warning_cap * 2);
			MapValidationEntry* next = (MapValidationEntry*)realloc(r->warnings, (size_t)next_cap * sizeof(*next));
			if (!next) {
				free(msg);
				return;
			}
			r->warnings = next;
			r->warning_cap = next_cap;
		}
		r->warnings[r->warning_count++] = e;
	}
}

static void json_write_context(FILE* out, const MapValidationContext* ctx) {
	bool first = true;
	fputc('{', out);
	if (ctx) {
		if (ctx->sector_index != -1) {
			fprintf(out, "%s\"sector_index\":%d", first ? "" : ",", ctx->sector_index);
			first = false;
		}
		if (ctx->wall_index != -1) {
			fprintf(out, "%s\"wall_index\":%d", first ? "" : ",", ctx->wall_index);
			first = false;
		}
		if (ctx->vertex_index != -1) {
			fprintf(out, "%s\"vertex_index\":%d", first ? "" : ",", ctx->vertex_index);
			first = false;
		}
		if (ctx->door_index != -1) {
			fprintf(out, "%s\"door_index\":%d", first ? "" : ",", ctx->door_index);
			first = false;
		}
		if (ctx->entity_index != -1) {
			fprintf(out, "%s\"entity_index\":%d", first ? "" : ",", ctx->entity_index);
			first = false;
		}
		if (ctx->light_index != -1) {
			fprintf(out, "%s\"light_index\":%d", first ? "" : ",", ctx->light_index);
			first = false;
		}
		if (!isnan(ctx->x)) {
			fprintf(out, "%s\"x\":%.3f", first ? "" : ",", ctx->x);
			first = false;
		}
		if (!isnan(ctx->y)) {
			fprintf(out, "%s\"y\":%.3f", first ? "" : ",", ctx->y);
			first = false;
		}
	}
	fputc('}', out);
}

static void json_write_entries(FILE* out, const MapValidationEntry* entries, int count) {
	fputc('[', out);
	for (int i = 0; i < count; i++) {
		const MapValidationEntry* e = &entries[i];
		if (i > 0) {
			fputc(',', out);
		}
		fputc('{', out);
		fputs("\"code\":", out);
		json_write_escaped_string(out, e->code ? e->code : "");
		fputs(",\"message\":", out);
		json_write_escaped_string(out, e->message ? e->message : "");
		fputs(",\"context\":", out);
		json_write_context(out, &e->context);
		fputc('}', out);
	}
	fputc(']', out);
}

static void cli_dump_map_spec(FILE* out) {
	// Single JSON object; stable ordering.
	fputc('{', out);
	fputs("\"command\":\"dump-map-spec\"", out);

	fputs(",\"top_level\":{", out);
	fputs("\"required_fields\":[\"player_start\",\"vertices\",\"sectors\",\"walls\"]", out);
	fputs(",\"optional_fields\":[\"bgmusic\",\"soundfont\",\"sky\",\"lights\",\"sounds\",\"particles\",\"entities\",\"doors\"]", out);
	fputs(",\"ignored_fields\":[\"version\",\"name\",\"textures\",\"flags\"]", out);
	fputs("}", out);

	fputs(",\"player_start\":{", out);
	fputs("\"type\":\"object\",\"required\":[\"x\",\"y\",\"angle_deg\"],\"properties\":{", out);
	fputs("\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"},\"angle_deg\":{\"type\":\"number\"}", out);
	fputs("}", out);
	fputs(",\"constraints\":[", out);
	fputs("\"(x,y) must be inside at least one sector\"", out);
	fputs(",\"All sectors must be reachable from the player_start sector via portal adjacency (back_sector != -1)\"", out);
	fputs("]}", out);

	fputs(",\"vertices\":{", out);
	fputs("\"type\":\"array\",\"min_items\":3,\"items\":{\"type\":\"object\",\"required\":[\"x\",\"y\"],\"properties\":{\"x\":{\"type\":\"number\"},\"y\":{\"type\":\"number\"}}}}", out);

	fputs(",\"sectors\":{", out);
	fputs("\"type\":\"array\",\"min_items\":1,\"items\":{", out);
	fputs("\"type\":\"object\",\"required\":[\"id\",\"floor_z\",\"ceil_z\",\"floor_tex\",\"ceil_tex\",\"light\"],", out);
	fputs("\"optional\":[\"light_color\",\"movable\",\"floor_z_toggled_pos\"],", out);
	fputs("\"properties\":{", out);
	fputs("\"id\":{\"type\":\"integer\"}", out);
	fputs(",\"floor_z\":{\"type\":\"number\"}", out);
	fputs(",\"ceil_z\":{\"type\":\"number\"}", out);
	fputs(",\"floor_tex\":{\"type\":\"string\",\"non_empty\":true}", out);
	fputs(",\"ceil_tex\":{\"type\":\"string\",\"non_empty\":true}", out);
	fputs(",\"light\":{\"type\":\"number\"}", out);
	fputs(",\"light_color\":{\"type\":\"object\",\"properties\":{\"r\":{\"type\":\"number\"},\"g\":{\"type\":\"number\"},\"b\":{\"type\":\"number\"}}}", out);
	fputs(",\"movable\":{\"type\":\"boolean\",\"default\":false}", out);
	fputs(",\"floor_z_toggled_pos\":{\"type\":\"number\"}", out);
	fputs("}", out);
	fputs("}", out);
	fputs(",\"constraints\":[", out);
	fputs("\"ceil_z > floor_z\"", out);
	fputs(",\"floor_tex and ceil_tex must be non-empty\"", out);
	fputs(",\"If movable is true, floor_z_toggled_pos must be present\"", out);
	fputs(",\"Movable sectors require clearance: ceil_z > max(floor_z_origin, floor_z_toggled_pos) + 0.10\"", out);
	fputs(",\"Each sector must have at least one closed boundary loop formed by its front-side walls\"", out);
	fputs("]", out);
	fputs(",\"warnings\":[", out);
	fputs("\"Multiple closed loops (holes/obstacles)\"", out);
	fputs(",\"Open wall components (internal segments)\"", out);
	fputs("]}", out);

	fputs(",\"walls\":{", out);
	fputs("\"type\":\"array\",\"min_items\":1,\"items\":{", out);
	fputs("\"type\":\"object\",\"required\":[\"v0\",\"v1\",\"front_sector\",\"back_sector\",\"tex\"],", out);
	fputs("\"optional\":[\"end_level\",\"toggle_sector\",\"toggle_sector_id\",\"toggle_sector_oneshot\",\"active_tex\",\"toggle_sound\",\"toggle_sound_finish\",\"required_item\",\"required_item_missing_message\"],", out);
	fputs("\"properties\":{", out);
	fputs("\"v0\":{\"type\":\"integer\"},\"v1\":{\"type\":\"integer\"}", out);
	fputs(",\"front_sector\":{\"type\":\"integer\"},\"back_sector\":{\"type\":\"integer\",\"portal_if_ge_0\":true,\"solid_if_minus_1\":true}", out);
	fputs(",\"tex\":{\"type\":\"string\",\"non_empty\":true}", out);
	fputs(",\"end_level\":{\"type\":\"boolean\"}", out);
	fputs(",\"toggle_sector\":{\"type\":\"boolean\"}", out);
	fputs(",\"toggle_sector_id\":{\"type\":\"integer\",\"notes\":[\"Refers to sector id (not sector index)\",\"-1 means default to player's current sector\"]}", out);
	fputs(",\"toggle_sector_oneshot\":{\"type\":\"boolean\"}", out);
	fputs(",\"active_tex\":{\"type\":\"string\"}", out);
	fputs(",\"toggle_sound\":{\"type\":\"string\",\"extension\":\".wav\"}", out);
	fputs(",\"toggle_sound_finish\":{\"type\":\"string\",\"extension\":\".wav\"}", out);
	fputs(",\"required_item\":{\"type\":\"string\"}", out);
	fputs(",\"required_item_missing_message\":{\"type\":\"string\"}", out);
	fputs("}", out);
	fputs("}", out);
	fputs(",\"constraints\":[", out);
	fputs("\"v0/v1 indices in range and v0 != v1\"", out);
	fputs(",\"front_sector in range; back_sector is -1 or in range\"", out);
	fputs(",\"tex must be non-empty\"", out);
	fputs(",\"end_level cannot be combined with toggle_sector\"", out);
	fputs(",\"toggle_sector_id (if not -1) must match some sector.id\"", out);
	fputs("]", out);
	fputs(",\"semantics\":[", out);
	fputs("\"Solid wall if back_sector == -1; portal wall if back_sector >= 0\"", out);
	fputs(",\"Exterior should be sealed with solid walls to prevent escaping outside sectors\"", out);
	fputs("]}", out);

	fputs(",\"doors\":{", out);
	fputs("\"type\":\"array\",\"items\":{\"type\":\"object\",\"required\":[\"id\",\"wall_index\",\"tex\"],\"optional\":[\"starts_closed\",\"sound_open\",\"required_item\",\"required_item_missing_message\"],\"defaults\":{\"starts_closed\":true}}", out);
	fputs(",\"constraints\":[\"id must be unique\",\"wall_index must refer to a portal wall\",\"doors cannot bind to end_level walls\"]}", out);

	fputs(",\"lights\":{", out);
	fputs("\"type\":\"array\",\"items\":{\"type\":\"object\",\"required\":[\"x\",\"y\",\"radius\"],\"one_of_required\":[[\"brightness\"],[\"intensity\"]],\"optional\":[\"z\",\"color\",\"flicker\",\"seed\"],\"enums\":{\"flicker\":[\"none\",\"flame\",\"malfunction\"]}}", out);
	fputs(",\"warnings\":[\"Warn if a light is not inside any sector\"]}", out);

	fputs(",\"sounds\":{", out);
	fputs("\"type\":\"array\",\"items\":{\"type\":\"object\",\"required\":[\"x\",\"y\",\"sound\"],\"optional\":[\"loop\",\"spatial\",\"gain\"],\"defaults\":{\"loop\":false,\"spatial\":true,\"gain\":1.0}}}", out);

	fputs(",\"particles\":{", out);
	fputs("\"type\":\"array\",\"items\":{\"type\":\"object\",\"required\":[\"x\",\"y\",\"particle_life_ms\",\"emit_interval_ms\",\"start\",\"end\"],\"optional\":[\"z\",\"offset_jitter\",\"rotate\",\"image\",\"shape\"]}}", out);

	fputs(",\"entities\":{", out);
	fputs("\"type\":\"array\",\"items\":{\"type\":\"object\",\"required\":[\"x\",\"y\"],\"preferred\":[\"def\"],\"optional\":[\"yaw_deg\",\"type\"],\"notes\":[\"If def is present, loader requires entity be inside a sector (otherwise load fails)\"]}}", out);

	fputs(",\"global_invariants\":[", out);
	fputs("\"Each sector must have at least one closed boundary loop\"", out);
	fputs(",\"Portal edges should be authored as two directed walls (A→B and B→A) so both sectors form valid boundaries\"", out);
	fputs(",\"Contiguity: all sectors reachable from the player_start sector via portal adjacency (back_sector != -1)\"", out);
	fputs("]", out);

	fputs(",\"string_length_guidance\":{\"note\":\"Many filenames are stored in fixed char[64]; keep filenames <= 63 bytes where practical\"}", out);

	fputc('}', out);
}

static int cli_validate_map(FILE* out, const char* argv0, const char* input_path) {
	(void)argv0;
	if (!out) {
		return 2;
	}
	if (!input_path || input_path[0] == '\0') {
		// Missing path argument.
		MapValidationReport r;
		map_validation_report_init(&r);
		MapValidationContext ctx;
		ctx.sector_index = -1;
		ctx.wall_index = -1;
		ctx.vertex_index = -1;
		ctx.door_index = -1;
		ctx.entity_index = -1;
		ctx.light_index = -1;
		ctx.x = NAN;
		ctx.y = NAN;
		report_push_entry(&r, true, "CLI_MISSING_PATH", "--validate-map requires a path argument", ctx);

		fputc('{', out);
		fputs("\"command\":\"validate-map\"", out);
		fputs(",\"input_path\":", out);
		json_write_escaped_string(out, input_path ? input_path : "");
		fputs(",\"map_filename\":", out);
		json_write_escaped_string(out, "");
		fputs(",\"status\":\"errors\"", out);
		fputs(",\"exit_code\":2", out);
		fputs(",\"errors\":", out);
		json_write_entries(out, r.errors, r.error_count);
		fputs(",\"warnings\":", out);
		json_write_entries(out, r.warnings, r.warning_count);
		fputc('}', out);
		map_validation_report_destroy(&r);
		return 2;
	}

	if (!log_init(LOG_LEVEL_ERROR)) {
		return 2;
	}

	char map_filename[1024];
	map_filename[0] = '\0';
	MapValidationReport rep;
	map_validation_report_init(&rep);

	if (!normalize_map_filename_arg(input_path, map_filename, sizeof(map_filename))) {
		MapValidationContext ctx;
		ctx.sector_index = -1;
		ctx.wall_index = -1;
		ctx.vertex_index = -1;
		ctx.door_index = -1;
		ctx.entity_index = -1;
		ctx.light_index = -1;
		ctx.x = NAN;
		ctx.y = NAN;
		report_push_entry(
			&rep,
			true,
			"CLI_INVALID_PATH",
			"Path must be a safe relative .json under Assets/Levels, or a filesystem path containing Assets/Levels/ that can be converted",
			ctx
		);
		int exit_code = 2;
		fputc('{', out);
		fputs("\"command\":\"validate-map\"", out);
		fputs(",\"input_path\":", out);
		json_write_escaped_string(out, input_path);
		fputs(",\"map_filename\":", out);
		json_write_escaped_string(out, "");
		fputs(",\"status\":\"errors\"", out);
		fprintf(out, ",\"exit_code\":%d", exit_code);
		fputs(",\"errors\":", out);
		json_write_entries(out, rep.errors, rep.error_count);
		fputs(",\"warnings\":", out);
		json_write_entries(out, rep.warnings, rep.warning_count);
		fputc('}', out);
		map_validation_report_destroy(&rep);
		log_shutdown();
		return exit_code;
	}

	char exe_dir[PATH_MAX];
	exe_dir[0] = '\0';
	(void)get_exe_dir(exe_dir, sizeof(exe_dir), argv0);

	AssetPaths paths;
	asset_paths_init(&paths, exe_dir[0] != '\0' ? exe_dir : NULL);

	MapLoadResult map;
	map_validate_set_report_sink(&rep);
	bool ok = map_load(&map, &paths, map_filename);
	map_validate_set_report_sink(NULL);

	if (!ok) {
		MapValidationContext ctx;
		ctx.sector_index = -1;
		ctx.wall_index = -1;
		ctx.vertex_index = -1;
		ctx.door_index = -1;
		ctx.entity_index = -1;
		ctx.light_index = -1;
		ctx.x = NAN;
		ctx.y = NAN;
		report_push_entry(&rep, true, "MAP_LOAD_FAILED", "map_load() returned false", ctx);
	}

	if (ok) {
		map_load_result_destroy(&map);
	}

	int exit_code = 0;
	const char* status = "ok";
	if (rep.error_count > 0) {
		exit_code = 2;
		status = "errors";
	} else if (rep.warning_count > 0) {
		exit_code = 1;
		status = "warnings";
	}

	fputc('{', out);
	fputs("\"command\":\"validate-map\"", out);
	fputs(",\"input_path\":", out);
	json_write_escaped_string(out, input_path);
	fputs(",\"map_filename\":", out);
	json_write_escaped_string(out, map_filename);
	fputs(",\"status\":", out);
	json_write_escaped_string(out, status);
	fprintf(out, ",\"exit_code\":%d", exit_code);
	fputs(",\"errors\":", out);
	json_write_entries(out, rep.errors, rep.error_count);
	fputs(",\"warnings\":", out);
	json_write_entries(out, rep.warnings, rep.warning_count);
	fputc('}', out);

	asset_paths_destroy(&paths);
	map_validation_report_destroy(&rep);
	log_shutdown();
	return exit_code;
}

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

static float cross2f(float ax, float ay, float bx, float by) {
	return ax * by - ay * bx;
}

// Returns true if segment P (p0->p1) intersects segment Q (q0->q1).
// If true, out_t is the param along P in (0,1].
static bool segment_intersect_param(
	float p0x,
	float p0y,
	float p1x,
	float p1y,
	float q0x,
	float q0y,
	float q1x,
	float q1y,
	float* out_t
) {
	float rx = p1x - p0x;
	float ry = p1y - p0y;
	float sx = q1x - q0x;
	float sy = q1y - q0y;
	float denom = cross2f(rx, ry, sx, sy);
	if (fabsf(denom) < 1e-8f) {
		return false;
	}
	float qpx = q0x - p0x;
	float qpy = q0y - p0y;
	float t = cross2f(qpx, qpy, sx, sy) / denom;
	float u = cross2f(qpx, qpy, rx, ry) / denom;
	if (t > 1e-6f && t <= 1.0f + 1e-6f && u >= -1e-6f && u <= 1.0f + 1e-6f) {
		if (out_t) {
			*out_t = t;
		}
		return true;
	}
	return false;
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

static bool key_pressed_no_repeat(const Input* in, int scancode) {
	if (!in) {
		return false;
	}
	for (int i = 0; i < in->key_event_count; i++) {
		if (in->key_events[i].scancode == scancode && !in->key_events[i].repeat) {
			return true;
		}
	}
	return false;
}

static void consume_key(Input* in, int scancode) {
        if (!in || scancode < 0 || scancode >= (int)(sizeof(in->keys_down) / sizeof(in->keys_down[0]))) {
                return;
        }
        // Prevent "held" semantics from leaking into the rest of the frame.
        in->keys_down[scancode] = false;
        // Remove any discrete KEYDOWN events for this scancode from this frame.
        int w = 0;
        for (int r = 0; r < in->key_event_count; r++) {
                if (in->key_events[r].scancode != scancode) {
                        in->key_events[w++] = in->key_events[r];
                }
        }
        in->key_event_count = w;
}

static uint32_t mix_gore_seed(uint32_t a, uint32_t b) {
        uint32_t h = a ^ (b + 0x9E3779B9u + (a << 6) + (a >> 2));
        h ^= h << 13;
        h ^= h >> 7;
        h ^= h << 17;
        return h ? h : 0xAC1D3Eu;
}

static float gore_body_height_center(const Entity* target) {
        if (!target) {
                return 0.8f;
        }
        return target->body.z + fmaxf(target->body.height * 0.6f, 0.1f);
}

static uint32_t gore_rng_step(uint32_t* s) {
        uint32_t x = *s;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        *s = x ? x : 0xA53B1Du;
        return *s;
}

static float gore_randf01(uint32_t* s) {
        return (float)(gore_rng_step(s) & 0xFFFFFFu) / (float)0x1000000u;
}

static void gore_basis_from_normal(float nx, float ny, float nz, float* rx, float* ry, float* rz, float* ux, float* uy, float* uz) {
        float n_len = sqrtf(nx * nx + ny * ny + nz * nz);
        if (n_len < 1e-5f) {
                nx = 0.0f;
                ny = 0.0f;
                nz = 1.0f;
                n_len = 1.0f;
        }
        nx /= n_len;
        ny /= n_len;
        nz /= n_len;
        float ax = fabsf(nx) > 0.8f ? 0.0f : 1.0f;
        float ay = 0.0f;
        float az = fabsf(nx) > 0.8f ? 1.0f : 0.0f;
        float r_x = ny * az - nz * ay;
        float r_y = nz * ax - nx * az;
        float r_z = nx * ay - ny * ax;
        float rl = sqrtf(r_x * r_x + r_y * r_y + r_z * r_z);
        if (rl < 1e-5f) {
                r_x = 1.0f;
                r_y = 0.0f;
                r_z = 0.0f;
                rl = 1.0f;
        }
        r_x /= rl;
        r_y /= rl;
        r_z /= rl;
        float u_x = r_y * nz - r_z * ny;
        float u_y = r_z * nx - r_x * nz;
        float u_z = r_x * ny - r_y * nx;
        float ul = sqrtf(u_x * u_x + u_y * u_y + u_z * u_z);
        if (ul > 1e-6f) {
                u_x /= ul;
                u_y /= ul;
                u_z /= ul;
        }
        if (rx) {
                *rx = r_x;
        }
        if (ry) {
                *ry = r_y;
        }
        if (rz) {
                *rz = r_z;
        }
        if (ux) {
                *ux = u_x;
        }
        if (uy) {
                *uy = u_y;
        }
        if (uz) {
                *uz = u_z;
        }
}

static void gore_pick_palette(uint32_t* rng, float* r, float* g, float* b) {
        static const float palette[4][3] = {
                {1.0f, 1.0f, 1.0f},
                {0.98f, 0.64f, 0.70f},
                {0.95f, 0.05f, 0.05f},
                {0.35f, 0.04f, 0.06f},
        };
        // Bias towards reds, with pink as an accent and white the rarest.
        float roll = gore_randf01(rng);
        int idx = 0;
        if (roll < 0.05f) {
                idx = 0; // white
        } else if (roll < 0.20f) {
                idx = 1; // pink
        } else if (roll < 0.60f) {
                idx = 2; // bright red
        } else {
                idx = 3; // dark maroon
        }
        *r = palette[idx][0];
        *g = palette[idx][1];
        *b = palette[idx][2];
}

static void gore_emit_chunk_burst(
        World* world,
        float x,
        float y,
        float z,
        float nx,
        float ny,
        float nz,
        int count,
        float base_speed,
        float speed_jitter,
        float spread_deg,
        uint32_t seed,
        int last_valid_sector) {
        if (!world || !world->gore.initialized || count <= 0) {
                return;
        }
        uint32_t rng = seed ? seed : 0xC11DB10Du;
        float rx = 1.0f, ry = 0.0f, rz = 0.0f;
        float ux = 0.0f, uy = 1.0f, uz = 0.0f;
        gore_basis_from_normal(nx, ny, nz, &rx, &ry, &rz, &ux, &uy, &uz);
        float spread_rad = spread_deg * (float)M_PI / 180.0f;

        for (int i = 0; i < count; i++) {
            float theta = gore_randf01(&rng) * spread_rad;
            float phi = gore_randf01(&rng) * 2.0f * (float)M_PI;
            float sin_t = sinf(theta);
            float cos_t = cosf(theta);
            float dir_x = nx * cos_t + (rx * cosf(phi) + ux * sinf(phi)) * sin_t;
            float dir_y = ny * cos_t + (ry * cosf(phi) + uy * sinf(phi)) * sin_t;
            float dir_z = nz * cos_t + (rz * cosf(phi) + uz * sinf(phi)) * sin_t;
            float dir_len = sqrtf(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
            if (dir_len > 1e-5f) {
                    dir_x /= dir_len;
                    dir_y /= dir_len;
                    dir_z /= dir_len;
            }
            float speed = base_speed + (gore_randf01(&rng) * 2.0f - 1.0f) * speed_jitter;
            float vx = dir_x * speed;
            float vy = dir_y * speed;
            float vz = dir_z * speed + 0.5f * base_speed * gore_randf01(&rng);
            // Mix in chunky variety (1x/2x/4x) with light per-tier jitter.
            float size_roll = gore_randf01(&rng);
            float size_mul = size_roll < 0.60f ? 1.0f : (size_roll < 0.90f ? 2.0f : 4.0f);
            float base_radius = 0.025f + gore_randf01(&rng) * 0.02f;
            float radius = base_radius * size_mul;
            float cr = 1.0f, cg = 0.0f, cb = 0.0f;
            gore_pick_palette(&rng, &cr, &cg, &cb);
            (void)gore_spawn_chunk(&world->gore, world, x, y, z, vx, vy, vz, radius, cr, cg, cb, 2800u, last_valid_sector);
        }
}

static void gore_emit_damage_splatter(World* world, const Entity* target, const PhysicsBody* player_body, float hx, float hy, uint32_t seed) {
        if (!world || !world->gore.initialized || !target) {
                return;
        }
        float base_x = hx;
        float base_y = hy;
        if (base_x == 0.0f && base_y == 0.0f) {
                base_x = target->body.x;
                base_y = target->body.y;
        }
        float center_z = gore_body_height_center(target);

        float nx = 0.0f;
        float ny = 0.0f;
        if (player_body) {
                float dx = target->body.x - player_body->x;
                float dy = target->body.y - player_body->y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 1e-3f) {
                        nx = dx / len;
                        ny = dy / len;
                }
        }
        float burst_dir_z = 0.6f;
        uint32_t s0 = mix_gore_seed(seed, (uint32_t)target->id.index + 1u);
        gore_emit_chunk_burst(world, base_x, base_y, center_z, nx, ny, burst_dir_z, 18, 6.5f, 3.0f, 55.0f, s0, target->body.last_valid_sector);
}

static void gore_emit_death_burst(World* world, const Entity* target, const PhysicsBody* player_body, uint32_t seed) {
        if (!world || !world->gore.initialized || !target) {
                return;
        }
        float base_x = target->body.x;
        float base_y = target->body.y;
        float center_z = gore_body_height_center(target);

        float nx = 0.0f;
        float ny = 0.0f;
        if (player_body) {
                float dx = target->body.x - player_body->x;
                float dy = target->body.y - player_body->y;
                float len = sqrtf(dx * dx + dy * dy);
                if (len > 1e-3f) {
                        nx = dx / len;
                        ny = dy / len;
                }
        }

        uint32_t s0 = mix_gore_seed(seed, (uint32_t)target->id.index + 11u);
        gore_emit_chunk_burst(world, base_x, base_y, center_z, nx, ny, 0.8f, 36, 8.0f, 3.5f, 85.0f, s0, target->body.last_valid_sector);
}

static void set_mouse_capture(Window* win, const CoreConfig* cfg, bool captured) {
	if (!win || !win->window) {
		return;
	}
	if (captured) {
		SDL_SetWindowGrab(win->window, (cfg && cfg->window.grab_mouse) ? SDL_TRUE : SDL_FALSE);
		SDL_SetRelativeMouseMode((cfg && cfg->window.relative_mouse) ? SDL_TRUE : SDL_FALSE);
		SDL_ShowCursor(SDL_DISABLE);
	} else {
		SDL_SetWindowGrab(win->window, SDL_FALSE);
		SDL_SetRelativeMouseMode(SDL_FALSE);
		SDL_ShowCursor(SDL_ENABLE);
	}
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
	bool cli_dump_spec = false;
	const char* cli_validate_path = NULL;
	for (int i = 1; i < argc; i++) {
		const char* a = argv[i];
		if (!a || a[0] == '\0') {
			continue;
		}
		if (strcmp(a, "--dump-map-spec") == 0) {
			cli_dump_spec = true;
			continue;
		}
		if (strcmp(a, "--validate-map") == 0) {
			if (i + 1 < argc) {
				cli_validate_path = argv[i + 1];
				i++; // consume value
			} else {
				cli_validate_path = ""; // triggers a structured error
			}
			continue;
		}
	}
	if (cli_dump_spec || cli_validate_path) {
		if (cli_dump_spec) {
			cli_dump_map_spec(stdout);
			if (cli_validate_path) {
				fputc('\n', stdout);
			}
		}
		if (cli_validate_path) {
			return cli_validate_map(stdout, (argc > 0) ? argv[0] : NULL, cli_validate_path);
		}
		return 0;
	}

	char prev_bgmusic[64] = "";
	char prev_soundfont[64] = "";

	const char* config_path_arg = NULL;
	const char* map_name_arg = NULL;
	const char* scene_name_arg = NULL;
	bool exit_after_scene = false;
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
		if (strcmp(a, "--scene") == 0) {
			if (i + 1 < argc) {
				scene_name_arg = argv[i + 1];
				exit_after_scene = true;
				i++; // consume value
			}
			continue;
		}
		if (strncmp(a, "CONFIG=", 7) == 0) {
			config_path_arg = a + 7;
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
	crash_diag_init();
	crash_diag_set_phase(PHASE_BOOT_SCENES_RUNNING);

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
	bool music_enabled = true;

	// Runtime toggles controlled by the console.

	FontSystem ui_font;
	if (!font_system_init(&ui_font, cfg->ui.font.file, cfg->ui.font.size_px, cfg->ui.font.atlas_size, cfg->ui.font.atlas_size, &paths)) {
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
	ParticleEmitters particle_emitters;
	particle_emitters_init(&particle_emitters);

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
	set_mouse_capture(&win, cfg, cfg->window.grab_mouse || cfg->window.relative_mouse);
	bool mouse_captured = cfg->window.grab_mouse || cfg->window.relative_mouse;
	bool suppress_fire_until_release = false;
	Screen* tab_menu_screen = NULL;

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

	Timeline timeline;
	memset(&timeline, 0, sizeof(timeline));
	bool timeline_ok = false;
	TimelineFlow tl_flow;
	timeline_flow_init(&tl_flow);
	MapLoadResult map;
	memset(&map, 0, sizeof(map));
	Doors doors;
	doors_init(&doors);
	bool map_ok = false;
	char map_name_buf[64] = "";
	bool using_timeline = false;
	if (scene_name_arg) {
		// Standalone scene mode: do not load timelines or maps.
		using_timeline = false;
		map_ok = false;
		map_name_buf[0] = '\0';
	} else {
		// Timeline mode: load a timeline asset now; TimelineFlow decides what to run first.
		if (cfg->content.boot_timeline[0] != '\0') {
			timeline_ok = timeline_load(&timeline, &paths, cfg->content.boot_timeline);
		}
	}
	if (!scene_name_arg && map_name_arg) {
		// A filename relative to Assets/Levels/ (e.g. "mortum_test.json").
		strncpy(map_name_buf, map_name_arg, sizeof(map_name_buf));
		map_name_buf[sizeof(map_name_buf) - 1] = '\0';
		// Explicit map arg overrides content.boot_timeline.
		using_timeline = false;
		tl_flow.active = false;
	}
	if (map_name_buf[0] != '\0') {
		crash_diag_set_phase(PHASE_MAP_LOAD_BEGIN);
		map_ok = map_load(&map, &paths, map_name_buf);
		crash_diag_set_phase(map_ok ? PHASE_MAP_INIT_WORLD : PHASE_MAP_LOAD_BEGIN);
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
		if (map_ok) {
			if (!doors_build_from_map(&doors, &map.world, map.doors, map.door_count)) {
				log_error("Doors failed to build (continuing without doors)");
			}
		}
		// Validate MIDI and SoundFont existence for background music
		crash_diag_set_phase(PHASE_AUDIO_TRACK_SWITCH_BEGIN);
		game_map_music_maybe_start(&paths, &map, map_ok, audio_enabled, music_enabled, prev_bgmusic, sizeof(prev_bgmusic), prev_soundfont, sizeof(prev_soundfont));
		crash_diag_set_phase(PHASE_AUDIO_TRACK_SWITCH_END);
	}

	LevelMesh mesh;
	level_mesh_init(&mesh);
	if (map_ok) {
		level_mesh_build(&mesh, &map.world);
	}

	TextureRegistry texreg;
	texture_registry_init(&texreg);

	float* wall_depth = NULL;
	float* depth_pixels = NULL;

	HudSystem hud;
	memset(&hud, 0, sizeof(hud));
	if (!hud_system_init(&hud, cfg, &paths, &texreg)) {
		log_error("HUD init failed; aborting startup");
		texture_registry_destroy(&texreg);
		level_mesh_destroy(&mesh);
		doors_destroy(&doors);
		if (map_ok) {
			map_load_result_destroy(&map);
			map_ok = false;
		}
		free(wall_depth);
		free(depth_pixels);
		present_shutdown(&presenter);
		framebuffer_destroy(&fb);
		window_destroy(&win);
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		font_system_shutdown(&ui_font);
		sound_emitters_shutdown(&sfx_emitters);
		particle_emitters_shutdown(&particle_emitters);
		sfx_shutdown();
		midi_shutdown();
		free(config_path);
		platform_shutdown();
		log_shutdown();
		return 1;
	}

	PostFxSystem postfx;
	postfx_init(&postfx);

	Notifications notifications;
	notifications_init(&notifications);
	float gameplay_time_s = 0.0f;
	notifications_reset(&notifications);

	Player player;
	player_init(&player);
	if (map_ok) {
			level_start_apply(&player, &map);
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

	// Spawn map-authored particle emitters.
	if (map_ok) {
		particle_emitters_reset(&particle_emitters);
		if (map.particles && map.particle_count > 0) {
			for (int i = 0; i < map.particle_count; i++) {
				MapParticleEmitter* mp = &map.particles[i];
				(void)particle_emitter_create(&particle_emitters, &map.world, mp->x, mp->y, mp->z, &mp->def);
			}
		}
	}

	crash_diag_set_phase(PHASE_MAP_SPAWN_ENTITIES_BEGIN);
	entity_system_reset(&entities, map_ok ? &map.world : NULL, map_ok ? &particle_emitters : NULL, &entity_defs);
	if (map_ok && map.entities && map.entity_count > 0) {
		entity_system_spawn_map(&entities, map.entities, map.entity_count);
	}
	crash_diag_set_phase(PHASE_MAP_SPAWN_ENTITIES_END);

	Input in;
	memset(&in, 0, sizeof(in));

	GameState gs;
	game_state_init(&gs);

	wall_depth = (float*)malloc((size_t)fb.width * sizeof(float));
	if (!wall_depth) {
		log_error("out of memory allocating depth buffer");
	}
	depth_pixels = (float*)malloc((size_t)fb.width * (size_t)fb.height * sizeof(float));
	if (!depth_pixels) {
		log_error("out of memory allocating per-pixel depth buffer");
	}

	GameLoop loop;
	game_loop_init(&loop, 1.0 / 60.0);

	bool running = true;
	int frames = 0;
	double fps_t0 = platform_time_seconds();
	int fps = 0;

	// Runtime toggles controlled by the console.
	bool show_debug = false;
	bool show_fps = false;
	bool show_font_test = false;
	bool light_emitters_enabled = cfg->render.point_lights_enabled;
	bool sound_emitters_enabled = true;


	PerfTrace perf;
	perf_trace_init(&perf);

	raycast_set_point_lights_enabled(light_emitters_enabled);
	sound_emitters_set_enabled(&sfx_emitters, audio_enabled && sound_emitters_enabled);

	Console console;
	console_init(&console);
	console_commands_register_all(&console);

	ConsoleCommandContext console_ctx;
	memset(&console_ctx, 0, sizeof(console_ctx));
	console_ctx.running = &running;
	console_ctx.argc = argc;
	console_ctx.argv = argv;
	console_ctx.config_path = config_path;
	console_ctx.paths = &paths;
	console_ctx.win = &win;
	console_ctx.mouse_captured = &mouse_captured;
	console_ctx.texreg = &texreg;
	console_ctx.hud = &hud;
	console_ctx.cfg = &cfg;
	console_ctx.audio_enabled = &audio_enabled;
	console_ctx.music_enabled = &music_enabled;
	console_ctx.sound_emitters_enabled = &sound_emitters_enabled;
	console_ctx.light_emitters_enabled = &light_emitters_enabled;
	console_ctx.show_fps = &show_fps;
	console_ctx.show_debug = &show_debug;
	console_ctx.show_font_test = &show_font_test;
	console_ctx.map = &map;
	console_ctx.map_ok = &map_ok;
	console_ctx.map_name_buf = map_name_buf;
	console_ctx.map_name_cap = sizeof(map_name_buf);
	console_ctx.using_timeline = &using_timeline;
	console_ctx.timeline = &timeline;
	console_ctx.tl_flow = &tl_flow;
	console_ctx.mesh = &mesh;
	console_ctx.player = &player;
	console_ctx.gs = &gs;
	console_ctx.entities = &entities;
	console_ctx.entity_defs = &entity_defs;
	console_ctx.sfx_emitters = &sfx_emitters;
	console_ctx.particle_emitters = &particle_emitters;
	console_ctx.perf = &perf;
	console_ctx.fb = &fb;
	console_ctx.wall_depth = wall_depth;
	console_ctx.prev_bgmusic = prev_bgmusic;
	console_ctx.prev_bgmusic_cap = sizeof(prev_bgmusic);
	console_ctx.prev_soundfont = prev_soundfont;
	console_ctx.prev_soundfont_cap = sizeof(prev_soundfont);
	console_ctx.notifications = &notifications;
	console_ctx.doors = &doors;
	console_ctx.gameplay_time_s = &gameplay_time_s;

	ScreenRuntime screens;
	screen_runtime_init(&screens);
	console_ctx.screens = &screens;

	// Start timeline-driven flow unless overridden by --scene or an explicit map arg.
	if (!scene_name_arg && !map_name_arg && timeline_ok) {
		TimelineFlowRuntime rt;
		memset(&rt, 0, sizeof(rt));
		rt.paths = &paths;
		rt.con = &console;
		rt.timeline = &timeline;
		rt.using_timeline = &using_timeline;
		rt.map = &map;
		rt.map_ok = &map_ok;
		rt.map_name_buf = map_name_buf;
		rt.map_name_cap = sizeof(map_name_buf);
		rt.mesh = &mesh;
		rt.player = &player;
		rt.gs = &gs;
		rt.entities = &entities;
		rt.entity_defs = &entity_defs;
		rt.sfx_emitters = &sfx_emitters;
		rt.particle_emitters = &particle_emitters;
		rt.doors = &doors;
		rt.screens = &screens;
		rt.fb = &fb;
		rt.console_ctx = &console_ctx;
		rt.notifications = &notifications;
		rt.in = NULL;
		rt.allow_scene_input = true;
		rt.audio_enabled = audio_enabled;
		rt.music_enabled = music_enabled;
		rt.sound_emitters_enabled = sound_emitters_enabled;
		rt.prev_bgmusic = prev_bgmusic;
		rt.prev_bgmusic_cap = sizeof(prev_bgmusic);
		rt.prev_soundfont = prev_soundfont;
		rt.prev_soundfont_cap = sizeof(prev_soundfont);
		(void)timeline_flow_start(&tl_flow, &rt);
	}

	// If launched with --scene, load and activate the scene now.
	if (scene_name_arg && scene_name_arg[0] != '\0') {
		Scene scene;
		if (!scene_load(&scene, &paths, scene_name_arg)) {
			log_error("Failed to load scene: %s", scene_name_arg);
			running = false;
		} else {
			Screen* scr = scene_screen_create(scene);
			if (!scr) {
				log_error("Failed to create scene screen");
				scene_destroy(&scene);
				running = false;
			} else {
				ScreenContext sctx;
				memset(&sctx, 0, sizeof(sctx)); 
				sctx.preserve_midi_on_exit = false;
				sctx.fb = &fb;
				sctx.in = &in;
				sctx.paths = &paths;
				sctx.allow_input = true;
				sctx.audio_enabled = audio_enabled;
				sctx.music_enabled = music_enabled;
				screen_runtime_set(&screens, scr, &sctx);
			}
		}
	}
	bool q_prev_down = false;
	bool e_prev_down = false;
	bool esc_prev_down = false;
	bool win_prev = false;
	bool lose_prev = false;
	uint32_t mouse_prev_buttons = 0u;
	double particle_ms_remainder = 0.0;


	while (running) {
		double frame_t0 = platform_time_seconds();
		double now = frame_t0;
		double prev_time = loop.last_time_s;
                double update_t0 = 0.0, update_t1 = 0.0;
                double render3d_t0 = 0.0, render3d_t1 = 0.0;
                double ui_t0 = 0.0, ui_t1 = 0.0;
                double present_t0 = 0.0, present_t1 = 0.0;
                double pe_update_ms = 0.0;
                double p_tick_ms = 0.0;
                double p_draw_ms = 0.0;
                double g_tick_ms = 0.0;
                double g_draw_ms = 0.0;
		int steps = game_loop_begin_frame(&loop, now);
		double frame_dt_s = 0.0;
		if (prev_time != 0.0) {
			frame_dt_s = now - prev_time;
			if (frame_dt_s < 0.0) {
				frame_dt_s = 0.0;
			}
			if (frame_dt_s > 0.25) {
				frame_dt_s = 0.25;
			}
		}

		input_begin_frame(&in);
		input_poll(&in);
		uint32_t mouse_pressed = in.mouse_buttons & ~mouse_prev_buttons;
		mouse_prev_buttons = in.mouse_buttons;
		// Toggle console with tilde / grave.
		if (input_key_pressed(&in, SDL_SCANCODE_GRAVE)) {
			console_set_open(&console, !console_is_open(&console));
		}
		bool console_open = console_is_open(&console);
		if (console_open) {
			console_blink_update(&console, (float)frame_dt_s);
			console_update(&console, &in, &console_ctx);
			// console_update() may close the console (e.g. via --close)
			console_open = console_is_open(&console);
		}

		bool screen_active = screen_runtime_is_active(&screens);
		if (tab_menu_screen && (!screen_active || screens.active != tab_menu_screen)) {
			// The screen was closed or replaced (e.g. via menu action).
			tab_menu_screen = NULL;
		}
		if (screen_active) {
			crash_diag_set_phase(PHASE_BOOT_SCENES_RUNNING);
			// Post-FX is gameplay-only; never persist into menus/scenes.
			postfx_reset(&postfx);
		}
		if (in.quit_requested) {
			running = false;
			if (audio_enabled) {
				midi_stop();
			}
		}
		// Mouse capture control:
		// - When captured: pressing input.bindings.release_mouse releases to the OS.
		// - When released: clicking in the window recaptures.
		bool released_this_frame = false;
		int release_sc = cfg ? cfg->input.release_mouse : (int)SDL_SCANCODE_ESCAPE;
		if (mouse_captured && release_sc != (int)SDL_SCANCODE_UNKNOWN && key_pressed_no_repeat(&in, release_sc)) {
			set_mouse_capture(&win, cfg, false);
			mouse_captured = false;
			released_this_frame = true;
			consume_key(&in, release_sc);
		}
		bool recaptured_this_frame = false;
		bool click_pressed = (mouse_pressed & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0u;
		if (!mouse_captured && click_pressed) {
			set_mouse_capture(&win, cfg, true);
			mouse_captured = true;
			recaptured_this_frame = true;
			// Consume this click so it never triggers gameplay/UI actions.
			in.mouse_buttons &= ~SDL_BUTTON(SDL_BUTTON_LEFT);
			// If the user is holding the button down while capturing, avoid firing until it is released.
			suppress_fire_until_release = true;
		}
		if (suppress_fire_until_release && (in.mouse_buttons & SDL_BUTTON(SDL_BUTTON_LEFT)) == 0u) {
			suppress_fire_until_release = false;
		}

		// Main menu toggle: input.bindings.open_main_menu opens/dismisses the main menu.
		int menu_sc = cfg ? cfg->input.open_main_menu : (int)SDL_SCANCODE_TAB;
		if (!console_open && menu_sc != (int)SDL_SCANCODE_UNKNOWN && key_pressed_no_repeat(&in, menu_sc)) {
			if (screen_active && tab_menu_screen && screens.active == tab_menu_screen) {
				ScreenContext sctx;
				memset(&sctx, 0, sizeof(sctx));
				sctx.preserve_midi_on_exit = false;
				sctx.fb = &fb;
				sctx.in = &in;
				sctx.paths = &paths;
				sctx.allow_input = true;
				sctx.audio_enabled = audio_enabled;
				sctx.music_enabled = music_enabled;
				screen_runtime_set(&screens, NULL, &sctx);
				tab_menu_screen = NULL;
				consume_key(&in, menu_sc);
				// Refresh screen_active after closing.
				screen_active = screen_runtime_is_active(&screens);
			} else if (!screen_active) {
				const char* menu_file = (using_timeline && timeline.pause_menu && timeline.pause_menu[0] != '\0') ? timeline.pause_menu : NULL;
				if (menu_file) {
					MenuAsset main_menu;
					if (!menu_load(&main_menu, &paths, menu_file)) {
						log_warn("Failed to load menu: %s", menu_file);
					} else {
						Screen* scr = menu_screen_create(main_menu, false, &console_ctx);
						if (!scr) {
							log_warn("Failed to create menu screen");
							menu_asset_destroy(&main_menu);
						} else {
							log_info_s("menu", "Opening menu via TAB: %s", menu_file);
							ScreenContext sctx;
							memset(&sctx, 0, sizeof(sctx));
							sctx.preserve_midi_on_exit = false;
							sctx.fb = &fb;
							sctx.in = &in;
							sctx.paths = &paths;
							sctx.allow_input = true;
							sctx.audio_enabled = audio_enabled;
							sctx.music_enabled = music_enabled;
							screen_runtime_set(&screens, scr, &sctx);
							tab_menu_screen = scr;
							consume_key(&in, menu_sc);
						}
					}
				}
				// Refresh screen_active after opening.
				screen_active = screen_runtime_is_active(&screens);
			}
		}

		// Pause menu toggle: during gameplay, Escape opens a menu screen.
		bool esc_down = (!console_open) && input_key_down(&in, SDL_SCANCODE_ESCAPE);
		bool esc_pressed = esc_down && !esc_prev_down;
		esc_prev_down = esc_down;
		bool suppress_pause_menu = released_this_frame && (release_sc == (int)SDL_SCANCODE_ESCAPE);
		if (running && !console_open && !screen_active && map_ok && esc_pressed && !suppress_pause_menu) {
			const char* menu_file = (using_timeline && timeline.pause_menu && timeline.pause_menu[0] != '\0') ? timeline.pause_menu : NULL;
			if (menu_file) {
				MenuAsset pause_menu;
				if (!menu_load(&pause_menu, &paths, menu_file)) {
					log_warn("Failed to load pause menu: %s", menu_file);
				} else {
					Screen* scr = menu_screen_create(pause_menu, false, &console_ctx);
					if (!scr) {
						log_warn("Failed to create pause menu screen");
						menu_asset_destroy(&pause_menu);
					} else {
						log_info_s("menu", "Opening pause menu via ESC: %s", menu_file);
						ScreenContext sctx;
						memset(&sctx, 0, sizeof(sctx));
						sctx.preserve_midi_on_exit = false;
						sctx.fb = &fb;
						sctx.in = &in;
						sctx.paths = &paths;
						sctx.allow_input = true;
						sctx.audio_enabled = audio_enabled;
						sctx.music_enabled = music_enabled;
						screen_runtime_set(&screens, scr, &sctx);
					}
				}
				// Refresh screen_active after opening.
				screen_active = screen_runtime_is_active(&screens);
			}
		}

		bool allow_game_input = !console_open;
		PlayerControllerInput ci = allow_game_input ? gather_controls(&in, &cfg->input) : (PlayerControllerInput){0};
		if (!allow_game_input || !mouse_captured) {
			ci.mouse_dx = 0.0f;
		}
		bool fire_down = (allow_game_input && mouse_captured && !recaptured_this_frame && !suppress_fire_until_release) ? gather_fire(&in) : false;
		uint8_t weapon_select_mask = 0;
		if (allow_game_input && input_key_down(&in, cfg->input.weapon_slot_1)) {
			weapon_select_mask |= 1u << 0;
		}
		if (allow_game_input && input_key_down(&in, cfg->input.weapon_slot_2)) {
			weapon_select_mask |= 1u << 1;
		}
		if (allow_game_input && input_key_down(&in, cfg->input.weapon_slot_3)) {
			weapon_select_mask |= 1u << 2;
		}
		if (allow_game_input && input_key_down(&in, cfg->input.weapon_slot_4)) {
			weapon_select_mask |= 1u << 3;
		}
		if (allow_game_input && input_key_down(&in, cfg->input.weapon_slot_5)) {
			weapon_select_mask |= 1u << 4;
		}
		int weapon_wheel_delta = allow_game_input ? in.mouse_wheel : 0;
		bool q_down = allow_game_input && input_key_down(&in, cfg->input.weapon_prev);
		bool e_down = allow_game_input && input_key_down(&in, cfg->input.weapon_next);
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

		if (screen_active) {
			if (perf_trace_is_active(&perf)) {
				update_t0 = platform_time_seconds();
			}
			ScreenContext sctx;
			memset(&sctx, 0, sizeof(sctx)); 
			sctx.preserve_midi_on_exit = timeline_flow_preserve_midi_on_scene_exit(&tl_flow);
			sctx.fb = &fb;
			sctx.in = &in;
			sctx.paths = &paths;
			sctx.allow_input = !console_open;
			sctx.audio_enabled = audio_enabled;
			sctx.music_enabled = music_enabled;
			bool completed = screen_runtime_update(&screens, &sctx, frame_dt_s);
			if (console_ctx.deferred_line_pending) {
				TimelineEvent* before_events = timeline.events;
				int before_event_count = timeline.event_count;
				bool before_flow_active = tl_flow.active;
				int before_flow_index = tl_flow.index;
				bool before_using_timeline = using_timeline;

				char line[CONSOLE_MAX_INPUT];
				strncpy(line, console_ctx.deferred_line, sizeof(line) - 1);
				line[sizeof(line) - 1] = '\0';
				console_ctx.deferred_line[0] = '\0';
				console_ctx.deferred_line_pending = false;
				(void)console_execute_line(&console, line, &console_ctx);

				bool changed_timeline_flow =
					(before_events != timeline.events) ||
					(before_event_count != timeline.event_count) ||
					(before_flow_active != tl_flow.active) ||
					(before_flow_index != tl_flow.index) ||
					(before_using_timeline != using_timeline);
				if (changed_timeline_flow) {
					// Avoid incorrectly advancing a newly-started flow (e.g. after load_timeline).
					completed = false;
				}
			}
			if (perf_trace_is_active(&perf)) {
				update_t1 = platform_time_seconds();
				ui_t0 = update_t1;
			}
			screen_runtime_draw(&screens, &sctx);
			if (completed && tab_menu_screen) {
				// If the active screen completed (e.g. ESC in menu), clear Tab-toggle state.
				tab_menu_screen = NULL;
			}
			if (show_fps) {
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
			console_draw(&console, &ui_font, &fb);
			if (cfg && cfg->render.vga_mode) {
				vga_palette_apply(&fb);
			}
			if (perf_trace_is_active(&perf)) {
				ui_t1 = platform_time_seconds();
				present_t0 = ui_t1;
			}
			present_frame(&presenter, &win, &fb);
			if (perf_trace_is_active(&perf)) {
				present_t1 = platform_time_seconds();
				double frame_t1 = present_t1;
				PerfTraceFrame pf = (PerfTraceFrame){0};
				pf.frame_ms = (frame_t1 - frame_t0) * 1000.0;
				pf.update_ms = (update_t1 - update_t0) * 1000.0;
				pf.render3d_ms = 0.0;
				pf.ui_ms = (ui_t1 - ui_t0) * 1000.0;
				pf.present_ms = (present_t1 - present_t0) * 1000.0;
				pf.steps = steps;
				perf_trace_record_frame(&perf, &pf, stdout);
			}
			if (completed && exit_after_scene) {
				running = false;
			}
			// If a scene overrode map music, restore the current map's MIDI when returning to gameplay.
			if (completed && !exit_after_scene) {
				crash_diag_set_phase(PHASE_AUDIO_TRACK_SWITCH_BEGIN);
				game_map_music_maybe_start(&paths, &map, map_ok, audio_enabled, music_enabled, prev_bgmusic, sizeof(prev_bgmusic), prev_soundfont, sizeof(prev_soundfont));
				crash_diag_set_phase(PHASE_AUDIO_TRACK_SWITCH_END);
			}
			// Timeline-driven scenes advance only when the active screen completes.
			if (completed && tl_flow.active && using_timeline && !exit_after_scene) {
				TimelineFlowRuntime rt;
				memset(&rt, 0, sizeof(rt));
				rt.paths = &paths;
				rt.con = &console;
				rt.timeline = &timeline;
				rt.using_timeline = &using_timeline;
				rt.map = &map;
				rt.map_ok = &map_ok;
				rt.map_name_buf = map_name_buf;
				rt.map_name_cap = sizeof(map_name_buf);
				rt.mesh = &mesh;
				rt.player = &player;
				rt.gs = &gs;
				rt.entities = &entities;
				rt.entity_defs = &entity_defs;
				rt.sfx_emitters = &sfx_emitters;
				rt.particle_emitters = &particle_emitters;
				rt.doors = &doors;
				rt.screens = &screens;
				rt.fb = &fb;
				rt.console_ctx = &console_ctx;
				rt.notifications = &notifications;
				rt.in = &in;
				rt.allow_scene_input = !console_open;
				rt.audio_enabled = audio_enabled;
				rt.music_enabled = music_enabled;
				rt.sound_emitters_enabled = sound_emitters_enabled;
				rt.prev_bgmusic = prev_bgmusic;
				rt.prev_bgmusic_cap = sizeof(prev_bgmusic);
				rt.prev_soundfont = prev_soundfont;
				rt.prev_soundfont_cap = sizeof(prev_soundfont);
				timeline_flow_on_screen_completed(&tl_flow, &rt);
			}
		} else {
			// Visual-only gameplay post-FX (damage flashes, status overlays, etc.)
			postfx_update(&postfx, frame_dt_s);

                if (map_ok) {
                        particle_emitters_begin_frame(&particle_emitters);
                        particles_begin_frame(&map.world.particles);
                        gore_begin_frame(&map.world.gore);
                }
		if (perf_trace_is_active(&perf)) {
			update_t0 = platform_time_seconds();
		}
		for (int i = 0; i < steps; i++) {
			if (gs.mode == GAME_MODE_PLAYING) {
				crash_diag_set_phase(PHASE_GAMEPLAY_UPDATE_TICK);
				float now_s = gameplay_time_s;
				bool action_down = key_down2(&in, cfg->input.action_primary, cfg->input.action_secondary);
				bool action_pressed = action_down && !player.action_prev_down;
				player.action_prev_down = action_down;
				if (action_pressed) {
					if (map_ok && sector_height_try_end_level_touching_wall(&map.world, &player, now_s)) {
						gs.mode = GAME_MODE_WIN;
					} else {
					bool opened_door = false;
					if (map_ok) {
						opened_door = doors_try_open_near_player(
							&doors,
							&map.world,
							&player,
							&notifications,
							&sfx_emitters,
							player.body.x,
							player.body.y,
							now_s
						);
					}
					if (!opened_door) {
					(void)sector_height_try_toggle_touching_wall(
						map_ok ? &map.world : NULL,
						&player,
						&sfx_emitters,
						&notifications,
						player.body.x,
						player.body.y,
						now_s
					);
					}
					}
				}
				sector_height_update(map_ok ? &map.world : NULL, &player, &sfx_emitters, player.body.x, player.body.y, loop.fixed_dt_s);
					if (map_ok) {
						doors_update(&doors, &map.world, now_s);
					}

				player_controller_update(&player, map_ok ? &map.world : NULL, &ci, loop.fixed_dt_s);
				entity_system_resolve_player_collisions(&entities, &player.body);

				entity_system_tick(&entities, &player.body, player.angle_deg, (float)loop.fixed_dt_s);
				gameplay_time_s += (float)loop.fixed_dt_s;
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
								} else if (def->u.pickup.type == PICKUP_TYPE_INVENTORY_ITEM) {
									(void)inventory_add_item(&player.inventory, def->u.pickup.add_to_inventory);
								}

								if (def->u.pickup.notification[0] != '\0') {
									(void)notifications_push_icon(&notifications, def->u.pickup.notification, def->sprite.file.name);
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
                                                                        if (map_ok && tdef->kind == ENTITY_KIND_ENEMY) {
                                                                                uint32_t gore_seed = mix_gore_seed((uint32_t)target->id.index, (uint32_t)ev->entity.index ^ (uint32_t)ev->amount);
                                                                                gore_emit_damage_splatter(&map.world, target, &player.body, ev->x, ev->y, gore_seed);
                                                                        }
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
                                                                if (map_ok && ev->kind == ENTITY_KIND_ENEMY) {
                                                                        Entity* target = NULL;
                                                                        if (entity_system_resolve(&entities, ev->entity, &target)) {
                                                                                uint32_t gore_seed = mix_gore_seed((uint32_t)ev->entity.index, (uint32_t)ev->other.index + 991u);
                                                                                gore_emit_death_burst(&map.world, target, &player.body, gore_seed);
                                                                        }
                                                                }
                                                                // Reserved for future: death sounds, drops, score, etc.
                                                        } break;

							case ENTITY_EVENT_PLAYER_DAMAGE: {
								// If a projectile hit the player, reuse its impact sound at the hit location.
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
									// Despawn already requested by entity tick, but request again is harmless.
									entity_system_request_despawn(&entities, ev->entity);
								}
								if (ev->amount > 0) {
									postfx_trigger_damage_flash(&postfx);
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

				// Particle emitters + particles (world-owned particles; emitters can be map- or entity-owned).
                                if (map_ok) {
                                        double ms = loop.fixed_dt_s * 1000.0 + particle_ms_remainder;
                                        uint32_t dt_ms = (uint32_t)ms;
                                        particle_ms_remainder = ms - (double)dt_ms;
                                        if (dt_ms > 0u) {
						double t0 = 0.0;
						if (perf_trace_is_active(&perf)) {
							t0 = platform_time_seconds();
						}
                                                particle_emitters_update(
                                                        &particle_emitters,
                                                        &map.world,
                                                        &map.world.particles,
                                                        player.body.x,
                                                        player.body.y,
                                                        player.body.sector,
                                                        dt_ms);
                                                if (perf_trace_is_active(&perf)) {
                                                        double t1 = platform_time_seconds();
                                                        pe_update_ms += (t1 - t0) * 1000.0;
                                                        t0 = t1;
                                                }
                                                particles_tick(&map.world.particles, dt_ms);
                                                if (perf_trace_is_active(&perf)) {
                                                        double t1 = platform_time_seconds();
                                                        p_tick_ms += (t1 - t0) * 1000.0;
                                                        t0 = t1;
                                                }
                                                gore_tick(&map.world.gore, &map.world, dt_ms);
                                                if (perf_trace_is_active(&perf)) {
                                                        double t1 = platform_time_seconds();
                                                        g_tick_ms += (t1 - t0) * 1000.0;
                                                }
                                        }
                                }

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
				bool use_down = allow_game_input && key_down2(&in, cfg->input.use_primary, cfg->input.use_secondary);
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

		notifications_tick(&notifications, (float)frame_dt_s);

		// One-shot death notification (avoid spam while in LOSE).
		bool lose_now = (gs.mode == GAME_MODE_LOSE && player.health <= 0);
		if (lose_now && !lose_prev) {
			(void)notifications_push_text(&notifications, "YOU DIED");
		}
		lose_prev = lose_now;

		// Timeline progression on win edge.
		bool win_now = (gs.mode == GAME_MODE_WIN);
		if (win_now && !win_prev && tl_flow.active && using_timeline) {
			TimelineFlowRuntime rt;
			memset(&rt, 0, sizeof(rt));
			rt.paths = &paths;
			rt.con = &console;
			rt.timeline = &timeline;
			rt.using_timeline = &using_timeline;
			rt.map = &map;
			rt.map_ok = &map_ok;
			rt.map_name_buf = map_name_buf;
			rt.map_name_cap = sizeof(map_name_buf);
			rt.mesh = &mesh;
			rt.player = &player;
			rt.gs = &gs;
			rt.entities = &entities;
			rt.entity_defs = &entity_defs;
			rt.sfx_emitters = &sfx_emitters;
			rt.particle_emitters = &particle_emitters;
			rt.screens = &screens;
			rt.fb = &fb;
			rt.console_ctx = &console_ctx;
			rt.notifications = &notifications;
			rt.in = &in;
			rt.allow_scene_input = true;
			rt.audio_enabled = audio_enabled;
			rt.music_enabled = music_enabled;
			rt.sound_emitters_enabled = sound_emitters_enabled;
			rt.prev_bgmusic = prev_bgmusic;
			rt.prev_bgmusic_cap = sizeof(prev_bgmusic);
			rt.prev_soundfont = prev_soundfont;
			rt.prev_soundfont_cap = sizeof(prev_soundfont);
			timeline_flow_on_map_win(&tl_flow, &rt);
		}
		win_prev = win_now;

		crash_diag_set_phase(PHASE_FIRST_FRAME_RENDER);
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
		// During step-up, PhysicsBody intentionally locks body.sector to the origin sector
		// while allowing body.x/y to advance. The raycaster assumes cam.x/y is inside the
		// start sector (and on the correct side of portal walls); when that invariant is
		// violated it can produce transient portal-edge rendering artifacts.
		if (
			map_ok &&
			player.body.step_up.active &&
			(unsigned)player.body.sector < (unsigned)map.world.sector_count &&
			(unsigned)player.body.step_up.to_sector < (unsigned)map.world.sector_count
		) {
			int from_sector = player.body.sector;
			int to_sector = player.body.step_up.to_sector;
			float frac = player.body.step_up.applied_frac;
			if (frac < 0.0f) {
				frac = 0.0f;
			} else if (frac > 1.0f) {
				frac = 1.0f;
			}
			// Reconstruct the point where the step started in world space.
			float origin_x = player.body.x - player.body.step_up.total_dx * frac;
			float origin_y = player.body.y - player.body.step_up.total_dy * frac;

			// First try a geometric clamp: keep cam on the from-sector side of the actual portal wall.
			float best_t = 1e30f;
			for (int i = 0; i < map.world.wall_count; i++) {
				Wall w = map.world.walls[i];
				if (w.back_sector < 0) {
					continue;
				}
				bool matches =
					(w.front_sector == from_sector && w.back_sector == to_sector) ||
					(w.front_sector == to_sector && w.back_sector == from_sector);
				if (!matches) {
					continue;
				}
				if (w.v0 < 0 || w.v0 >= map.world.vertex_count || w.v1 < 0 || w.v1 >= map.world.vertex_count) {
					continue;
				}
				Vertex a = map.world.vertices[w.v0];
				Vertex b = map.world.vertices[w.v1];
				float t = 0.0f;
				if (segment_intersect_param(origin_x, origin_y, cam.x, cam.y, a.x, a.y, b.x, b.y, &t)) {
					if (t < best_t) {
						best_t = t;
					}
				}
			}
			if (best_t < 1e20f) {
				// Pull back a tiny bit from the crossing so we stay on the from-sector side.
				float t = best_t - 1e-4f;
				if (t < 0.0f) {
					t = 0.0f;
				}
				cam.x = origin_x + (cam.x - origin_x) * t;
				cam.y = origin_y + (cam.y - origin_y) * t;
			} else {
				// Fallback: clamp using sector membership queries.
				int sec_now = world_find_sector_at_point(&map.world, cam.x, cam.y);
				if (sec_now != from_sector) {
					float lo = 0.0f;
					float hi = frac;
					for (int i = 0; i < 10; i++) {
						float mid = 0.5f * (lo + hi);
						float tx = origin_x + player.body.step_up.total_dx * mid;
						float ty = origin_y + player.body.step_up.total_dy * mid;
						int s = world_find_sector_at_point(&map.world, tx, ty);
						if (s == from_sector) {
							lo = mid;
						} else {
							hi = mid;
						}
					}
					cam.x = origin_x + player.body.step_up.total_dx * lo;
					cam.y = origin_y + player.body.step_up.total_dy * lo;
				}
			}
		}

		// Update looping ambient emitters with current listener position.
		sound_emitters_update(&sfx_emitters, cam.x, cam.y);
		int start_sector = -1;
		if (map_ok && (unsigned)player.body.sector < (unsigned)map.world.sector_count) {
			start_sector = player.body.sector;
			// Rendering bug fix (rare, 1-frame): the raycaster computes plane projection
			// (floor/ceiling) using a camera Z derived from the *start sector*'s floor/ceiling.
			// During boundary transitions (especially with camera smoothing / step-up clamping),
			// the camera position can land just across a portal for a frame while
			// player.body.sector still reflects the other side. If we render with the wrong
			// start sector, floor/ceiling can briefly jump into the middle of the screen.
			//
			// Solution: if the chosen start sector doesn't actually contain the camera, prefer
			// the sector-at-camera-point for this frame.
			if (!world_sector_contains_point(&map.world, start_sector, cam.x, cam.y)) {
				int cam_sector = world_find_sector_at_point(&map.world, cam.x, cam.y);
				if ((unsigned)cam_sector < (unsigned)map.world.sector_count) {
					start_sector = cam_sector;
				}
			}
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
                        if (perf_trace_is_active(&perf)) {
                                double t0 = platform_time_seconds();
                                gore_draw(&map.world.gore, &fb, &map.world, &cam, start_sector, wall_depth, depth_pixels);
                                double t1 = platform_time_seconds();
                                g_draw_ms += (t1 - t0) * 1000.0;
                                t0 = t1;
                                particles_draw(&map.world.particles, &fb, &map.world, &cam, start_sector, &texreg, &paths, wall_depth, depth_pixels);
                                t1 = platform_time_seconds();
                                p_draw_ms += (t1 - t0) * 1000.0;
                        } else {
                                gore_draw(&map.world.gore, &fb, &map.world, &cam, start_sector, wall_depth, depth_pixels);
                                particles_draw(&map.world.particles, &fb, &map.world, &cam, start_sector, &texreg, &paths, wall_depth, depth_pixels);
                        }
                }
		if (perf_trace_is_active(&perf)) {
			render3d_t1 = platform_time_seconds();
			ui_t0 = render3d_t1;
		}

		weapon_view_draw(&fb, &player, &texreg, &paths);
		postfx_draw(&postfx, &fb);
		hud_draw(&hud, &fb, &player, &gs, fps, &texreg, &paths);
		if (show_debug) {
			debug_overlay_draw(&ui_font, &fb, &player, map_ok ? &map.world : NULL, &entities, fps);
		}
		if (show_font_test) {
			font_draw_test_page(&ui_font, &fb, 16, 16);
		}
		if (show_fps) {
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
		notifications_draw(&notifications, &fb, &ui_font, &texreg, &paths);
		console_draw(&console, &ui_font, &fb);
		if (cfg && cfg->render.vga_mode) {
			vga_palette_apply(&fb);
		}
		if (perf_trace_is_active(&perf)) {
			ui_t1 = platform_time_seconds();
			present_t0 = ui_t1;
		}

		present_frame(&presenter, &win, &fb);
		if (perf_trace_is_active(&perf)) {
			present_t1 = platform_time_seconds();
			double frame_t1 = present_t1;
			PerfTraceFrame pf = (PerfTraceFrame){0};
			pf.frame_ms = (frame_t1 - frame_t0) * 1000.0;
			pf.update_ms = (update_t1 - update_t0) * 1000.0;
			pf.render3d_ms = (render3d_t1 - render3d_t0) * 1000.0;
			pf.ui_ms = (ui_t1 - ui_t0) * 1000.0;
			pf.present_ms = (present_t1 - present_t0) * 1000.0;
			pf.steps = steps;
                        pf.pe_update_ms = pe_update_ms;
                        pf.p_tick_ms = p_tick_ms;
                        pf.p_draw_ms = p_draw_ms;
                        pf.g_tick_ms = g_tick_ms;
                        pf.g_draw_ms = g_draw_ms;
                        pf.pe_alive = particle_emitters.alive_count;
                        pf.pe_emitters_updated = (int)particle_emitters.stats_emitters_updated;
                        pf.pe_emitters_gated = (int)particle_emitters.stats_emitters_gated;
                        pf.pe_spawn_attempted = (int)particle_emitters.stats_particles_spawn_attempted;
                        pf.p_alive = map_ok ? map.world.particles.alive_count : 0;
                        pf.p_capacity = map_ok ? map.world.particles.capacity : 0;
                        pf.p_spawned = map_ok ? (int)map.world.particles.stats_spawned : 0;
                        pf.p_dropped = map_ok ? (int)map.world.particles.stats_dropped : 0;
                        pf.p_drawn_particles = map_ok ? (int)map.world.particles.stats_drawn_particles : 0;
                        pf.p_pixels_written = map_ok ? (int)map.world.particles.stats_pixels_written : 0;
                        pf.g_alive = map_ok ? map.world.gore.alive_count : 0;
                        pf.g_capacity = map_ok ? map.world.gore.capacity : 0;
                        pf.g_spawned = map_ok ? (int)map.world.gore.stats_spawned : 0;
                        pf.g_dropped = map_ok ? (int)map.world.gore.stats_dropped : 0;
                        pf.g_drawn_samples = map_ok ? (int)map.world.gore.stats_drawn_samples : 0;
                        pf.g_pixels_written = map_ok ? (int)map.world.gore.stats_pixels_written : 0;
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
	}

		frames++;
		if (now - fps_t0 >= 1.0) {
			fps = frames;
			frames = 0;
			fps_t0 = now;
		}
	}

	{
		ScreenContext sctx;
		memset(&sctx, 0, sizeof(sctx)); 
		sctx.preserve_midi_on_exit = false;
		sctx.fb = &fb;
		sctx.in = &in;
		sctx.paths = &paths;
		sctx.allow_input = true;
		sctx.audio_enabled = audio_enabled;
		sctx.music_enabled = music_enabled;
		screen_runtime_shutdown(&screens, &sctx);
	}

	doors_destroy(&doors);
	if (map_ok) {
		map_load_result_destroy(&map);
	}
	timeline_destroy(&timeline);

	entity_system_shutdown(&entities);
	entity_defs_destroy(&entity_defs);

	hud_system_shutdown(&hud);
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
	particle_emitters_shutdown(&particle_emitters);
	sfx_shutdown();
	midi_shutdown(); // Clean up music resources
	free(config_path);
	platform_shutdown();
	log_shutdown();
	return 0;
}
