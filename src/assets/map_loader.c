#include "assets/map_loader.h"

#include "assets/json.h"
#include "assets/map_validate.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static uint32_t hash_u32(uint32_t x) {
	// SplitMix32
	x += 0x9E3779B9u;
	x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
	x = (x ^ (x >> 13)) * 0xC2B2AE35u;
	return x ^ (x >> 16);
}

static bool json_get_float(const JsonDoc* doc, int tok, float* out) {
	double d = 0.0;
	if (!json_get_double(doc, tok, &d)) {
		return false;
	}
	*out = (float)d;
	return true;
}

static bool json_get_light_color(const JsonDoc* doc, int tok, LightColor* out) {
	if (!json_token_is_object(doc, tok)) {
		return false;
	}
	int tr = -1, tg = -1, tb = -1;
	if (!json_object_get(doc, tok, "r", &tr) || !json_object_get(doc, tok, "g", &tg) || !json_object_get(doc, tok, "b", &tb)) {
		return false;
	}
	float r = 0, g = 0, b = 0;
	if (!json_get_float(doc, tr, &r) || !json_get_float(doc, tg, &g) || !json_get_float(doc, tb, &b)) {
		return false;
	}
	out->r = r;
	out->g = g;
	out->b = b;
	return true;
}

static bool hex_nibble(char c, uint8_t* out) {
	if (c >= '0' && c <= '9') {
		*out = (uint8_t)(c - '0');
		return true;
	}
	if (c >= 'a' && c <= 'f') {
		*out = (uint8_t)(10 + (c - 'a'));
		return true;
	}
	if (c >= 'A' && c <= 'F') {
		*out = (uint8_t)(10 + (c - 'A'));
		return true;
	}
	return false;
}

static bool parse_hex_color_sv(StringView sv, LightColor* out) {
	if (!out) {
		return false;
	}
	// Accept "RRGGBB" or "#RRGGBB".
	if (sv.len == 7 && sv.data[0] == '#') {
		sv.data++;
		sv.len--;
	}
	if (sv.len != 6) {
		return false;
	}
	uint8_t n0=0,n1=0,n2=0,n3=0,n4=0,n5=0;
	if (!hex_nibble(sv.data[0], &n0) || !hex_nibble(sv.data[1], &n1) ||
		!hex_nibble(sv.data[2], &n2) || !hex_nibble(sv.data[3], &n3) ||
		!hex_nibble(sv.data[4], &n4) || !hex_nibble(sv.data[5], &n5)) {
		return false;
	}
	uint8_t r = (uint8_t)((n0 << 4) | n1);
	uint8_t g = (uint8_t)((n2 << 4) | n3);
	uint8_t b = (uint8_t)((n4 << 4) | n5);
	out->r = (float)r / 255.0f;
	out->g = (float)g / 255.0f;
	out->b = (float)b / 255.0f;
	return true;
}

static bool json_get_light_color_any(const JsonDoc* doc, int tok, LightColor* out) {
	if (!doc || tok < 0 || tok >= doc->token_count || !out) {
		return false;
	}
	if (json_token_is_string(doc, tok)) {
		StringView sv;
		if (!json_get_string(doc, tok, &sv)) {
			return false;
		}
		return parse_hex_color_sv(sv, out);
	}
	return json_get_light_color(doc, tok, out);
}

static bool json_get_light_flicker(const JsonDoc* doc, int tok, LightFlicker* out) {
	if (!doc || tok < 0 || tok >= doc->token_count || !out) {
		return false;
	}
	if (!json_token_is_string(doc, tok)) {
		return false;
	}
	StringView sv;
	if (!json_get_string(doc, tok, &sv)) {
		return false;
	}
	if (sv.len == 4 && strncmp(sv.data, "none", 4) == 0) {
		*out = LIGHT_FLICKER_NONE;
		return true;
	}
	if (sv.len == 5 && strncmp(sv.data, "flame", 5) == 0) {
		*out = LIGHT_FLICKER_FLAME;
		return true;
	}
	if (sv.len == 10 && strncmp(sv.data, "malfunction", 10) == 0) {
		*out = LIGHT_FLICKER_MALFUNCTION;
		return true;
	}
	return false;
}

static bool json_get_bool(const JsonDoc* doc, int tok, bool* out) {
	if (!doc || tok < 0 || tok >= doc->token_count || !out) {
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
	return false;
}

void map_load_result_destroy(MapLoadResult* self) {
	world_destroy(&self->world);
	memset(self, 0, sizeof(*self));
}

bool map_load(MapLoadResult* out, const AssetPaths* paths, const char* map_filename) {
	memset(out, 0, sizeof(*out));
	world_init_empty(&out->world);

	char* full = asset_path_join(paths, "Levels", map_filename);
	if (!full) {
		return false;
	}
	JsonDoc doc;
	if (!json_doc_load_file(&doc, full)) {
		free(full);
		return false;
	}
	free(full);

	if (doc.token_count < 1 || !json_token_is_object(&doc, 0)) {
		log_error("Map JSON root must be an object");
		json_doc_destroy(&doc);
		return false;
	}

	// Parse optional bgmusic/soundfont/sky fields
	int t_bgmusic = -1, t_soundfont = -1, t_sky = -1;
	if (json_object_get(&doc, 0, "bgmusic", &t_bgmusic) && t_bgmusic != -1) {
		StringView sv_bgmusic;
		if (json_get_string(&doc, t_bgmusic, &sv_bgmusic)) {
			snprintf(out->bgmusic, sizeof(out->bgmusic), "%.*s", (int)sv_bgmusic.len, sv_bgmusic.data);
		}
	} else {
		out->bgmusic[0] = '\0';
	}
	if (json_object_get(&doc, 0, "soundfont", &t_soundfont) && t_soundfont != -1) {
		StringView sv_soundfont;
		if (json_get_string(&doc, t_soundfont, &sv_soundfont)) {
			snprintf(out->soundfont, sizeof(out->soundfont), "%.*s", (int)sv_soundfont.len, sv_soundfont.data);
		}
	} else {
		snprintf(out->soundfont, sizeof(out->soundfont), "hl4mgm.sf2");
	}
	if (json_object_get(&doc, 0, "sky", &t_sky) && t_sky != -1) {
		StringView sv_sky;
		if (json_get_string(&doc, t_sky, &sv_sky)) {
			snprintf(out->sky, sizeof(out->sky), "%.*s", (int)sv_sky.len, sv_sky.data);
		} else {
			out->sky[0] = '\0';
		}
	} else {
		out->sky[0] = '\0';
	}

	int t_player = -1;
	int t_vertices = -1;
	int t_sectors = -1;
	int t_walls = -1;
	int t_lights = -1;
	if (!json_object_get(&doc, 0, "player_start", &t_player) || !json_object_get(&doc, 0, "vertices", &t_vertices) || !json_object_get(&doc, 0, "sectors", &t_sectors) || !json_object_get(&doc, 0, "walls", &t_walls)) {
		log_error("Map JSON missing required fields");
		json_doc_destroy(&doc);
		return false;
	}
	(void)json_object_get(&doc, 0, "lights", &t_lights);

	// player_start
	if (!json_token_is_object(&doc, t_player)) {
		log_error("player_start must be an object");
		json_doc_destroy(&doc);
		return false;
	}
	int t_x=-1,t_y=-1,t_ang=-1;
	if (!json_object_get(&doc, t_player, "x", &t_x) || !json_object_get(&doc, t_player, "y", &t_y) || !json_object_get(&doc, t_player, "angle_deg", &t_ang)) {
		log_error("player_start missing x/y/angle_deg");
		json_doc_destroy(&doc);
		return false;
	}
	if (!json_get_float(&doc, t_x, &out->player_start_x) || !json_get_float(&doc, t_y, &out->player_start_y) || !json_get_float(&doc, t_ang, &out->player_start_angle_deg)) {
		log_error("player_start values invalid");
		json_doc_destroy(&doc);
		return false;
	}

	// vertices
	int vcount = json_array_size(&doc, t_vertices);
	if (vcount < 3) {
		log_error("vertices must have at least 3 entries");
		json_doc_destroy(&doc);
		return false;
	}
	world_alloc_vertices(&out->world, vcount);
	for (int i = 0; i < vcount; i++) {
		int tv = json_array_nth(&doc, t_vertices, i);
		int tx=-1, ty=-1;
		if (!json_object_get(&doc, tv, "x", &tx) || !json_object_get(&doc, tv, "y", &ty)) {
			log_error("vertex %d missing x/y", i);
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		float x=0, y=0;
		if (!json_get_float(&doc, tx, &x) || !json_get_float(&doc, ty, &y)) {
			log_error("vertex %d x/y invalid", i);
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		out->world.vertices[i].x = x;
		out->world.vertices[i].y = y;
	}

	// sectors
	int scount = json_array_size(&doc, t_sectors);
	if (scount < 1) {
		log_error("sectors must have at least 1 entry");
		json_doc_destroy(&doc);
		map_load_result_destroy(out);
		return false;
	}
	world_alloc_sectors(&out->world, scount);
	for (int i = 0; i < scount; i++) {
		int ts = json_array_nth(&doc, t_sectors, i);
		int tid=-1, tfloor=-1, tceil=-1, tfloor_tex=-1, tceil_tex=-1, tlight=-1;
		int tlight_color = -1;
		int tmovable = -1;
		int tfloor_toggled = -1;
		if (!json_object_get(&doc, ts, "id", &tid) || !json_object_get(&doc, ts, "floor_z", &tfloor) || !json_object_get(&doc, ts, "ceil_z", &tceil) || !json_object_get(&doc, ts, "floor_tex", &tfloor_tex) || !json_object_get(&doc, ts, "ceil_tex", &tceil_tex) || !json_object_get(&doc, ts, "light", &tlight)) {
			log_error("sector %d missing required fields", i);
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		(void)json_object_get(&doc, ts, "light_color", &tlight_color);
		(void)json_object_get(&doc, ts, "movable", &tmovable);
		(void)json_object_get(&doc, ts, "floor_z_toggled_pos", &tfloor_toggled);
		int id=0;
		float floor_z=0, ceil_z=0, light=1.0f;
		StringView sv_floor_tex, sv_ceil_tex;
		if (!json_get_int(&doc, tid, &id) || !json_get_float(&doc, tfloor, &floor_z) || !json_get_float(&doc, tceil, &ceil_z) || !json_get_float(&doc, tlight, &light) || !json_get_string(&doc, tfloor_tex, &sv_floor_tex) || !json_get_string(&doc, tceil_tex, &sv_ceil_tex)) {
			log_error("sector %d fields invalid", i);
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		bool movable = false;
		float floor_z_toggled_pos = floor_z;
		if (tmovable != -1) {
			if (!json_get_bool(&doc, tmovable, &movable)) {
				log_error("sector %d movable invalid (must be true/false)", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
		}
		if (tfloor_toggled != -1) {
			if (!json_get_float(&doc, tfloor_toggled, &floor_z_toggled_pos)) {
				log_error("sector %d floor_z_toggled_pos invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			movable = true; // presence implies movable
		}
		if (movable && tfloor_toggled == -1) {
			log_error("sector %d is movable but missing floor_z_toggled_pos", i);
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		out->world.sectors[i].id = id;
		out->world.sectors[i].floor_z = floor_z;
		out->world.sectors[i].floor_z_origin = floor_z;
		out->world.sectors[i].floor_z_toggled_pos = floor_z_toggled_pos;
		out->world.sectors[i].movable = movable;
		out->world.sectors[i].floor_moving = false;
		out->world.sectors[i].floor_z_target = floor_z;
		out->world.sectors[i].floor_toggle_wall_index = -1;
		out->world.sectors[i].ceil_z = ceil_z;
		out->world.sectors[i].light = light;
		out->world.sectors[i].light_color = light_color_white();
		if (tlight_color != -1) {
			LightColor lc = light_color_white();
			if (!json_get_light_color(&doc, tlight_color, &lc)) {
				log_error("sector %d light_color invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			out->world.sectors[i].light_color = lc;
		}
		world_set_sector_tex(&out->world.sectors[i], sv_floor_tex, sv_ceil_tex);
	}

	// optional point lights
	if (t_lights != -1) {
		if (!json_token_is_array(&doc, t_lights)) {
			log_error("lights must be an array");
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		int lcount = json_array_size(&doc, t_lights);
		if (lcount > 0) {
			if (!world_alloc_lights(&out->world, lcount)) {
				log_error("failed to allocate lights");
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			for (int i = 0; i < lcount; i++) {
				int tl = json_array_nth(&doc, t_lights, i);
				if (!json_token_is_object(&doc, tl)) {
					log_error("light %d must be an object", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				int tx=-1, ty=-1, tz=-1, tradius=-1;
				int tbrightness=-1, tintensity=-1;
				int tcolor=-1, tflicker=-1, tseed=-1;
				if (!json_object_get(&doc, tl, "x", &tx) || !json_object_get(&doc, tl, "y", &ty) || !json_object_get(&doc, tl, "radius", &tradius)) {
					log_error("light %d missing required fields (x,y,radius)", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				(void)json_object_get(&doc, tl, "brightness", &tbrightness);
				(void)json_object_get(&doc, tl, "intensity", &tintensity);
				if (tbrightness == -1 && tintensity == -1) {
					log_error("light %d missing required field (brightness)", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				(void)json_object_get(&doc, tl, "z", &tz);
				(void)json_object_get(&doc, tl, "color", &tcolor);
				(void)json_object_get(&doc, tl, "flicker", &tflicker);
				(void)json_object_get(&doc, tl, "seed", &tseed);

				float x=0, y=0, z=0, radius=0, intensity=0;
				if (!json_get_float(&doc, tx, &x) || !json_get_float(&doc, ty, &y) || !json_get_float(&doc, tradius, &radius)) {
					log_error("light %d fields invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				if (tbrightness != -1) {
					if (!json_get_float(&doc, tbrightness, &intensity)) {
						log_error("light %d brightness invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				} else {
					if (!json_get_float(&doc, tintensity, &intensity)) {
						log_error("light %d intensity invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}
				if (intensity < 0.0f) {
					intensity = 0.0f;
				}
				if (radius < 0.0f) {
					radius = 0.0f;
				}

				if (tz != -1) {
					if (!json_get_float(&doc, tz, &z)) {
						log_error("light %d z invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}
				LightColor lc = light_color_white();
				if (tcolor != -1) {
					if (!json_get_light_color_any(&doc, tcolor, &lc)) {
						log_error("light %d color invalid (expected hex string like \"EE0000\" or {r,g,b})", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}

				LightFlicker flicker = LIGHT_FLICKER_NONE;
				if (tflicker != -1) {
					if (!json_get_light_flicker(&doc, tflicker, &flicker)) {
						log_error("light %d flicker invalid (none|flame|malfunction)", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}

				uint32_t seed = 0u;
				if (tseed != -1) {
					int s = 0;
					if (!json_get_int(&doc, tseed, &s)) {
						log_error("light %d seed invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					seed = (uint32_t)s;
				} else {
					// Derive a stable-ish seed from authored properties to avoid sync.
					uint32_t hx = (uint32_t)lroundf(x * 1024.0f);
					uint32_t hy = (uint32_t)lroundf(y * 1024.0f);
					uint32_t hr = (uint32_t)lroundf(radius * 256.0f);
					seed = hash_u32((uint32_t)i ^ (hx * 0x9E3779B1u) ^ (hy * 0x85EBCA77u) ^ (hr * 0xC2B2AE3Du));
				}

				out->world.lights[i].x = x;
				out->world.lights[i].y = y;
				out->world.lights[i].z = z;
				out->world.lights[i].radius = radius;
				out->world.lights[i].intensity = intensity;
				out->world.lights[i].color = lc;
				out->world.lights[i].flicker = flicker;
				out->world.lights[i].seed = seed;
			}
		}
	}

	// walls
	int wcount = json_array_size(&doc, t_walls);
	if (wcount < 1) {
		log_error("walls must have at least 1 entry");
		json_doc_destroy(&doc);
		map_load_result_destroy(out);
		return false;
	}
	world_alloc_walls(&out->world, wcount);
	for (int i = 0; i < wcount; i++) {
		int tw = json_array_nth(&doc, t_walls, i);
		int tv0=-1,tv1=-1,tfs=-1,tbs=-1,ttex=-1;
		int t_toggle_sector = -1;
		int t_toggle_sector_id = -1;
		int t_toggle_sector_oneshot = -1;
		int t_active_tex = -1;
		if (!json_object_get(&doc, tw, "v0", &tv0) || !json_object_get(&doc, tw, "v1", &tv1) || !json_object_get(&doc, tw, "front_sector", &tfs) || !json_object_get(&doc, tw, "back_sector", &tbs) || !json_object_get(&doc, tw, "tex", &ttex)) {
			log_error("wall %d missing required fields", i);
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		(void)json_object_get(&doc, tw, "toggle_sector", &t_toggle_sector);
		(void)json_object_get(&doc, tw, "toggle_sector_id", &t_toggle_sector_id);
		(void)json_object_get(&doc, tw, "toggle_sector_oneshot", &t_toggle_sector_oneshot);
		(void)json_object_get(&doc, tw, "active_tex", &t_active_tex);
		int v0=0,v1=0,fs=0,bs=-1;
		StringView sv_tex;
		if (!json_get_int(&doc, tv0, &v0) || !json_get_int(&doc, tv1, &v1) || !json_get_int(&doc, tfs, &fs) || !json_get_int(&doc, tbs, &bs) || !json_get_string(&doc, ttex, &sv_tex)) {
			log_error("wall %d fields invalid", i);
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		bool toggle_sector = false;
		int toggle_sector_id = -1;
		bool toggle_sector_oneshot = false;
		if (t_toggle_sector != -1) {
			if (!json_get_bool(&doc, t_toggle_sector, &toggle_sector)) {
				log_error("wall %d toggle_sector invalid (must be true/false)", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
		}
		if (t_toggle_sector_id != -1) {
			if (!json_get_int(&doc, t_toggle_sector_id, &toggle_sector_id)) {
				log_error("wall %d toggle_sector_id invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
		}
		if (t_toggle_sector_oneshot != -1) {
			if (!json_get_bool(&doc, t_toggle_sector_oneshot, &toggle_sector_oneshot)) {
				log_error("wall %d toggle_sector_oneshot invalid (must be true/false)", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
		}
		StringView sv_active_tex;
		bool has_active_tex = false;
		if (t_active_tex != -1) {
			if (!json_get_string(&doc, t_active_tex, &sv_active_tex)) {
				log_error("wall %d active_tex invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			has_active_tex = true;
		}
		out->world.walls[i].v0 = v0;
		out->world.walls[i].v1 = v1;
		out->world.walls[i].front_sector = fs;
		out->world.walls[i].back_sector = bs;
		out->world.walls[i].toggle_sector = toggle_sector;
		out->world.walls[i].toggle_sector_id = toggle_sector_id;
		out->world.walls[i].toggle_sector_oneshot = toggle_sector_oneshot;
		out->world.walls[i].active_tex[0] = '\0';
		world_set_wall_tex(&out->world.walls[i], sv_tex);
		if (has_active_tex) {
			size_t n = sv_active_tex.len < 63 ? sv_active_tex.len : 63;
			memcpy(out->world.walls[i].active_tex, sv_active_tex.data, n);
			out->world.walls[i].active_tex[n] = '\0';
		}
	}

	json_doc_destroy(&doc);

	if (!map_validate(&out->world, out->player_start_x, out->player_start_y)) {
		map_load_result_destroy(out);
		return false;
	}

	(void)world_build_sector_wall_index(&out->world);

	return true;
}
