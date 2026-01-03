#include "assets/map_loader.h"

#include "assets/json.h"
#include "assets/map_validate.h"
#include "core/log.h"

#include "core/path_safety.h"

#include "game/particles.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static bool ends_with_ci(const char* s, const char* suffix) {
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
		char a = (char)tolower((unsigned char)tail[i]);
		char b = (char)tolower((unsigned char)suffix[i]);
		if (a != b) {
			return false;
		}
	}
	return true;
}

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
	if (sv.len == 11 && strncmp(sv.data, "malfunction", 11) == 0) {
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

static bool parse_hex_rgb(StringView sv, float* out_r, float* out_g, float* out_b) {
	if (!out_r || !out_g || !out_b) {
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
	unsigned v = 0;
	for (size_t i = 0; i < 6; i++) {
		char c = sv.data[i];
		unsigned d = 0;
		if (c >= '0' && c <= '9') {
			d = (unsigned)(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			d = 10u + (unsigned)(c - 'a');
		} else if (c >= 'A' && c <= 'F') {
			d = 10u + (unsigned)(c - 'A');
		} else {
			return false;
		}
		v = (v << 4u) | d;
	}
	unsigned r = (v >> 16u) & 0xFFu;
	unsigned g = (v >> 8u) & 0xFFu;
	unsigned b = v & 0xFFu;
	*out_r = (float)r / 255.0f;
	*out_g = (float)g / 255.0f;
	*out_b = (float)b / 255.0f;
	return true;
}

static bool json_get_particle_shape(const JsonDoc* doc, int tok, ParticleShape* out) {
	if (!doc || tok < 0 || tok >= doc->token_count || !out) {
		return false;
	}
	StringView svv;
	if (!json_get_string(doc, tok, &svv)) {
		return false;
	}
	if (svv.len == 6 && strncmp(svv.data, "circle", 6) == 0) {
		*out = PARTICLE_SHAPE_CIRCLE;
		return true;
	}
	if (svv.len == 6 && strncmp(svv.data, "square", 6) == 0) {
		*out = PARTICLE_SHAPE_SQUARE;
		return true;
	}
	return false;
}

static bool json_get_particle_color(const JsonDoc* doc, int tok, ParticleEmitterColor* out) {
	if (!doc || tok < 0 || tok >= doc->token_count || !out || !json_token_is_object(doc, tok)) {
		return false;
	}
	int t_value = -1;
	int t_opacity = -1;
	if (!json_object_get(doc, tok, "value", &t_value)) {
		return false;
	}
	(void)json_object_get(doc, tok, "opacity", &t_opacity);
	StringView svv;
	if (!json_get_string(doc, t_value, &svv)) {
		return false;
	}
	float r = 1.0f, g = 1.0f, b = 1.0f;
	if (!parse_hex_rgb(svv, &r, &g, &b)) {
		return false;
	}
	float opacity = 0.0f;
	if (t_opacity != -1) {
		if (!json_get_float(doc, t_opacity, &opacity)) {
			return false;
		}
	}
	out->r = r;
	out->g = g;
	out->b = b;
	out->opacity = opacity;
	return true;
}

static bool json_get_particle_keyframe(const JsonDoc* doc, int tok, ParticleEmitterKeyframe* out) {
	if (!doc || tok < 0 || tok >= doc->token_count || !out || !json_token_is_object(doc, tok)) {
		return false;
	}
	int t_opacity = -1;
	int t_color = -1;
	int t_size = -1;
	int t_offset = -1;
	if (!json_object_get(doc, tok, "opacity", &t_opacity) || !json_object_get(doc, tok, "color", &t_color) ||
		!json_object_get(doc, tok, "size", &t_size) || !json_object_get(doc, tok, "offset", &t_offset)) {
		return false;
	}
	float opacity = 1.0f;
	float size = 1.0f;
	if (!json_get_float(doc, t_opacity, &opacity) || !json_get_float(doc, t_size, &size)) {
		return false;
	}
	ParticleEmitterColor c;
	if (!json_get_particle_color(doc, t_color, &c)) {
		return false;
	}
	if (!json_token_is_object(doc, t_offset)) {
		return false;
	}
	int tx = -1, ty = -1, tz = -1;
	if (!json_object_get(doc, t_offset, "x", &tx) || !json_object_get(doc, t_offset, "y", &ty) || !json_object_get(doc, t_offset, "z", &tz)) {
		return false;
	}
	float ox = 0.0f, oy = 0.0f, oz = 0.0f;
	if (!json_get_float(doc, tx, &ox) || !json_get_float(doc, ty, &oy) || !json_get_float(doc, tz, &oz)) {
		return false;
	}
	out->opacity = opacity;
	out->color = c;
	out->size = size;
	out->offset.x = ox;
	out->offset.y = oy;
	out->offset.z = oz;
	return true;
}

static bool json_get_particle_rotate(const JsonDoc* doc, int tok, ParticleEmitterRotate* out) {
	if (!doc || tok < 0 || tok >= doc->token_count || !out || !json_token_is_object(doc, tok)) {
		return false;
	}
	out->enabled = false;
	out->tick.deg = 0.0f;
	out->tick.time_ms = 30;
	int t_enabled = -1;
	int t_tick = -1;
	(void)json_object_get(doc, tok, "enabled", &t_enabled);
	(void)json_object_get(doc, tok, "tick", &t_tick);
	if (t_enabled != -1) {
		if (!json_get_bool(doc, t_enabled, &out->enabled)) {
			return false;
		}
	}
	if (t_tick != -1) {
		if (!json_token_is_object(doc, t_tick)) {
			return false;
		}
		int t_deg = -1;
		int t_time = -1;
		(void)json_object_get(doc, t_tick, "deg", &t_deg);
		(void)json_object_get(doc, t_tick, "time_ms", &t_time);
		if (t_deg != -1 && !json_get_float(doc, t_deg, &out->tick.deg)) {
			return false;
		}
		if (t_time != -1 && !json_get_int(doc, t_time, &out->tick.time_ms)) {
			return false;
		}
	}
	return true;
}

void map_load_result_destroy(MapLoadResult* self) {
	if (!self) {
		return;
	}
	log_info_s(
		"map",
		"map_load_result_destroy: self=%p world={v=%p vc=%d s=%p sc=%d w=%p wc=%d} sounds=%p sc=%d particles=%p pc=%d entities=%p ec=%d doors=%p dc=%d",
		(void*)self,
		(void*)self->world.vertices,
		self->world.vertex_count,
		(void*)self->world.sectors,
		self->world.sector_count,
		(void*)self->world.walls,
		self->world.wall_count,
		(void*)self->sounds,
		self->sound_count,
		(void*)self->particles,
		self->particle_count,
		(void*)self->entities,
		self->entity_count,
		(void*)self->doors,
		self->door_count
	);
	free(self->sounds);
	self->sounds = NULL;
	self->sound_count = 0;
	free(self->particles);
	self->particles = NULL;
	self->particle_count = 0;
	free(self->entities);
	self->entities = NULL;
	self->entity_count = 0;
	free(self->doors);
	self->doors = NULL;
	self->door_count = 0;
	world_destroy(&self->world);
	memset(self, 0, sizeof(*self));
	log_info_s("map", "map_load_result_destroy: done self=%p", (void*)self);
}

bool map_load(MapLoadResult* out, const AssetPaths* paths, const char* map_filename) {
	if (!out || !paths || !map_filename || map_filename[0] == '\0') {
		return false;
	}
	if (!name_is_safe_relpath(map_filename) || !ends_with_ci(map_filename, ".json")) {
		log_error("Map filename must be a safe relative .json path under Assets/Levels: %s", map_filename);
		return false;
	}
	memset(out, 0, sizeof(*out));
	world_init_empty(&out->world);
	if (!particles_init(&out->world.particles, PARTICLE_MAX_DEFAULT)) {
		return false;
	}

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
	int t_sounds = -1;
	int t_particles = -1;
	int t_entities = -1;
	int t_doors = -1;
	if (!json_object_get(&doc, 0, "player_start", &t_player) || !json_object_get(&doc, 0, "vertices", &t_vertices) || !json_object_get(&doc, 0, "sectors", &t_sectors) || !json_object_get(&doc, 0, "walls", &t_walls)) {
		log_error("Map JSON missing required fields");
		json_doc_destroy(&doc);
		return false;
	}
	(void)json_object_get(&doc, 0, "lights", &t_lights);
	(void)json_object_get(&doc, 0, "sounds", &t_sounds);
	(void)json_object_get(&doc, 0, "particles", &t_particles);
	(void)json_object_get(&doc, 0, "entities", &t_entities);
	(void)json_object_get(&doc, 0, "doors", &t_doors);

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

	// (remaining parse continues...) 

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

	// optional sound emitters
	if (t_sounds != -1) {
		if (!json_token_is_array(&doc, t_sounds)) {
			log_error("sounds must be an array");
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		int scount = json_array_size(&doc, t_sounds);
		if (scount > 0) {
			out->sounds = (MapSoundEmitter*)calloc((size_t)scount, sizeof(MapSoundEmitter));
			if (!out->sounds) {
				log_error("failed to allocate sounds");
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			out->sound_count = scount;
			for (int i = 0; i < scount; i++) {
				int ts = json_array_nth(&doc, t_sounds, i);
				if (!json_token_is_object(&doc, ts)) {
					log_error("sound %d must be an object", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				int tx=-1, ty=-1, tsound=-1;
				int tloop=-1, tspatial=-1, tgain=-1;
				if (!json_object_get(&doc, ts, "x", &tx) || !json_object_get(&doc, ts, "y", &ty) || !json_object_get(&doc, ts, "sound", &tsound)) {
					log_error("sound %d missing required fields (x,y,sound)", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				(void)json_object_get(&doc, ts, "loop", &tloop);
				(void)json_object_get(&doc, ts, "spatial", &tspatial);
				(void)json_object_get(&doc, ts, "gain", &tgain);

				float x = 0.0f, y = 0.0f;
				StringView sv_sound;
				if (!json_get_float(&doc, tx, &x) || !json_get_float(&doc, ty, &y) || !json_get_string(&doc, tsound, &sv_sound)) {
					log_error("sound %d fields invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				bool loop = false;
				bool spatial = true;
				float gain = 1.0f;
				if (tloop != -1 && !json_get_bool(&doc, tloop, &loop)) {
					log_error("sound %d loop invalid (must be true/false)", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				if (tspatial != -1 && !json_get_bool(&doc, tspatial, &spatial)) {
					log_error("sound %d spatial invalid (must be true/false)", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				if (tgain != -1 && !json_get_float(&doc, tgain, &gain)) {
					log_error("sound %d gain invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				if (gain < 0.0f) {
					gain = 0.0f;
				}
				if (gain > 1.0f) {
					gain = 1.0f;
				}

				out->sounds[i].x = x;
				out->sounds[i].y = y;
				out->sounds[i].loop = loop;
				out->sounds[i].spatial = spatial;
				out->sounds[i].gain = gain;
				size_t n = sv_sound.len < 63 ? sv_sound.len : 63;
				memcpy(out->sounds[i].sound, sv_sound.data, n);
				out->sounds[i].sound[n] = '\0';
			}
		}
	}

	// optional particle emitters
	if (t_particles != -1) {
		if (!json_token_is_array(&doc, t_particles)) {
			log_error("particles must be an array");
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		int pcount = json_array_size(&doc, t_particles);
		if (pcount > 0) {
			out->particles = (MapParticleEmitter*)calloc((size_t)pcount, sizeof(MapParticleEmitter));
			if (!out->particles) {
				log_error("failed to allocate particles");
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			out->particle_count = pcount;
			for (int i = 0; i < pcount; i++) {
				int tp = json_array_nth(&doc, t_particles, i);
				if (!json_token_is_object(&doc, tp)) {
					log_error("particle emitter %d must be an object", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				int tx=-1, ty=-1;
				int tz=-1;
				int t_life=-1;
				int t_interval=-1;
				int t_jitter=-1;
				int t_rotate=-1;
				int t_image=-1;
				int t_shape=-1;
				int t_start=-1;
				int t_end=-1;
				if (!json_object_get(&doc, tp, "x", &tx) || !json_object_get(&doc, tp, "y", &ty) ||
					!json_object_get(&doc, tp, "particle_life_ms", &t_life) || !json_object_get(&doc, tp, "emit_interval_ms", &t_interval) ||
					!json_object_get(&doc, tp, "start", &t_start) || !json_object_get(&doc, tp, "end", &t_end)) {
					log_error("particle emitter %d missing required fields", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				(void)json_object_get(&doc, tp, "z", &tz);
				(void)json_object_get(&doc, tp, "offset_jitter", &t_jitter);
				(void)json_object_get(&doc, tp, "rotate", &t_rotate);
				(void)json_object_get(&doc, tp, "image", &t_image);
				(void)json_object_get(&doc, tp, "shape", &t_shape);

				float x = 0.0f, y = 0.0f, z = 0.0f;
				if (!json_get_float(&doc, tx, &x) || !json_get_float(&doc, ty, &y)) {
					log_error("particle emitter %d x/y invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				if (tz != -1) {
					if (!json_get_float(&doc, tz, &z)) {
						log_error("particle emitter %d z invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}

				int life_ms = 0;
				int interval_ms = 0;
				if (!json_get_int(&doc, t_life, &life_ms) || !json_get_int(&doc, t_interval, &interval_ms)) {
					log_error("particle emitter %d particle_life_ms/emit_interval_ms invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				float jitter = 0.0f;
				if (t_jitter != -1) {
					if (!json_get_float(&doc, t_jitter, &jitter)) {
						log_error("particle emitter %d offset_jitter invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}

				ParticleEmitterRotate rot;
				rot.enabled = false;
				rot.tick.deg = 0.0f;
				rot.tick.time_ms = 30;
				if (t_rotate != -1) {
					if (!json_get_particle_rotate(&doc, t_rotate, &rot)) {
						log_error("particle emitter %d rotate invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}

				char image[64];
				image[0] = '\0';
				if (t_image != -1) {
					StringView sv_img;
					if (!json_get_string(&doc, t_image, &sv_img)) {
						log_error("particle emitter %d image invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					size_t n = sv_img.len < sizeof(image) - 1 ? sv_img.len : sizeof(image) - 1;
					memcpy(image, sv_img.data, n);
					image[n] = '\0';
				}

				ParticleShape shape = PARTICLE_SHAPE_CIRCLE;
				if (t_shape != -1) {
					if (!json_get_particle_shape(&doc, t_shape, &shape)) {
						log_error("particle emitter %d shape invalid (circle|square)", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
				}

				ParticleEmitterKeyframe start;
				ParticleEmitterKeyframe end;
				if (!json_get_particle_keyframe(&doc, t_start, &start) || !json_get_particle_keyframe(&doc, t_end, &end)) {
					log_error("particle emitter %d start/end invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}

				out->particles[i].x = x;
				out->particles[i].y = y;
				out->particles[i].z = z;
				out->particles[i].def.particle_life_ms = life_ms;
				out->particles[i].def.emit_interval_ms = interval_ms;
				out->particles[i].def.offset_jitter = jitter;
				out->particles[i].def.rotate = rot;
				out->particles[i].def.shape = shape;
				out->particles[i].def.start = start;
				out->particles[i].def.end = end;
				strncpy(out->particles[i].def.image, image, sizeof(out->particles[i].def.image) - 1);
				out->particles[i].def.image[sizeof(out->particles[i].def.image) - 1] = '\0';
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
		int t_required_item = -1;
		int t_required_item_missing_message = -1;
		int t_toggle_sound = -1;
		int t_toggle_sound_finish = -1;
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
		(void)json_object_get(&doc, tw, "required_item", &t_required_item);
		(void)json_object_get(&doc, tw, "required_item_missing_message", &t_required_item_missing_message);
		(void)json_object_get(&doc, tw, "toggle_sound", &t_toggle_sound);
		(void)json_object_get(&doc, tw, "toggle_sound_finish", &t_toggle_sound_finish);
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
		out->world.walls[i].required_item[0] = '\0';
		out->world.walls[i].required_item_missing_message[0] = '\0';
		out->world.walls[i].toggle_sound[0] = '\0';
		out->world.walls[i].toggle_sound_finish[0] = '\0';
		world_set_wall_tex(&out->world.walls[i], sv_tex);
		if (has_active_tex) {
			size_t n = sv_active_tex.len < 63 ? sv_active_tex.len : 63;
			memcpy(out->world.walls[i].active_tex, sv_active_tex.data, n);
			out->world.walls[i].active_tex[n] = '\0';
		}
		if (t_required_item != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_required_item, &sv)) {
				log_error("wall %d required_item invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			if (sv.len >= sizeof(out->world.walls[i].required_item)) {
				log_error("wall %d required_item too long (max %zu)", i, sizeof(out->world.walls[i].required_item) - 1u);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			size_t n = sv.len;
			memcpy(out->world.walls[i].required_item, sv.data, n);
			out->world.walls[i].required_item[n] = '\0';
		}
		if (t_required_item_missing_message != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_required_item_missing_message, &sv)) {
				log_error("wall %d required_item_missing_message invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			if (sv.len >= sizeof(out->world.walls[i].required_item_missing_message)) {
				log_error("wall %d required_item_missing_message too long (max %zu)", i, sizeof(out->world.walls[i].required_item_missing_message) - 1u);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			size_t n = sv.len;
			memcpy(out->world.walls[i].required_item_missing_message, sv.data, n);
			out->world.walls[i].required_item_missing_message[n] = '\0';
		}
		if (t_toggle_sound != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_toggle_sound, &sv)) {
				log_error("wall %d toggle_sound invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			size_t n = sv.len < 63 ? sv.len : 63;
			memcpy(out->world.walls[i].toggle_sound, sv.data, n);
			out->world.walls[i].toggle_sound[n] = '\0';
		}
		if (t_toggle_sound_finish != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_toggle_sound_finish, &sv)) {
				log_error("wall %d toggle_sound_finish invalid", i);
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			size_t n = sv.len < 63 ? sv.len : 63;
			memcpy(out->world.walls[i].toggle_sound_finish, sv.data, n);
			out->world.walls[i].toggle_sound_finish[n] = '\0';
		}
	}

	// optional doors
	if (t_doors != -1) {
		if (!json_token_is_array(&doc, t_doors)) {
			log_error("doors must be an array");
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		int dcount = json_array_size(&doc, t_doors);
		if (dcount > 0) {
			out->doors = (MapDoor*)calloc((size_t)dcount, sizeof(MapDoor));
			if (!out->doors) {
				log_error("Out of memory allocating doors");
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			out->door_count = dcount;
			for (int i = 0; i < dcount; i++) {
				int td = json_array_nth(&doc, t_doors, i);
				if (!json_token_is_object(&doc, td)) {
					log_error("door %d must be an object", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				int t_id = -1;
				int t_wall_index = -1;
				int t_tex = -1;
				int t_starts_closed = -1;
				int t_sound_open = -1;
				int t_required_item = -1;
				int t_required_item_missing_message = -1;
				if (!json_object_get(&doc, td, "id", &t_id) || !json_object_get(&doc, td, "wall_index", &t_wall_index) || !json_object_get(&doc, td, "tex", &t_tex)) {
					log_error("door %d missing required fields (id, wall_index, tex)", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				(void)json_object_get(&doc, td, "starts_closed", &t_starts_closed);
				(void)json_object_get(&doc, td, "sound_open", &t_sound_open);
				(void)json_object_get(&doc, td, "required_item", &t_required_item);
				(void)json_object_get(&doc, td, "required_item_missing_message", &t_required_item_missing_message);

				StringView sv_id;
				StringView sv_tex;
				int wall_index = -1;
				if (!json_get_string(&doc, t_id, &sv_id) || !json_get_int(&doc, t_wall_index, &wall_index) || !json_get_string(&doc, t_tex, &sv_tex)) {
					log_error("door %d fields invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				if (sv_id.len == 0 || sv_id.len >= sizeof(out->doors[i].id)) {
					log_error("door %d id invalid length (max %zu)", i, sizeof(out->doors[i].id) - 1u);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				if (sv_tex.len == 0 || sv_tex.len >= sizeof(out->doors[i].tex)) {
					log_error("door %d tex invalid length (max %zu)", i, sizeof(out->doors[i].tex) - 1u);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				memcpy(out->doors[i].id, sv_id.data, sv_id.len);
				out->doors[i].id[sv_id.len] = '\0';
				memcpy(out->doors[i].tex, sv_tex.data, sv_tex.len);
				out->doors[i].tex[sv_tex.len] = '\0';
				out->doors[i].wall_index = wall_index;
				out->doors[i].starts_closed = true;
				out->doors[i].sound_open[0] = '\0';
				out->doors[i].required_item[0] = '\0';
				out->doors[i].required_item_missing_message[0] = '\0';
				if (t_starts_closed != -1) {
					bool b = true;
					if (!json_get_bool(&doc, t_starts_closed, &b)) {
						log_error("door %d starts_closed invalid (must be true/false)", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					out->doors[i].starts_closed = b;
				}
				if (t_sound_open != -1) {
					StringView sv;
					if (!json_get_string(&doc, t_sound_open, &sv)) {
						log_error("door %d sound_open invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					if (sv.len >= sizeof(out->doors[i].sound_open)) {
						log_error("door %d sound_open too long (max %zu)", i, sizeof(out->doors[i].sound_open) - 1u);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					memcpy(out->doors[i].sound_open, sv.data, sv.len);
					out->doors[i].sound_open[sv.len] = '\0';
				}
				if (t_required_item != -1) {
					StringView sv;
					if (!json_get_string(&doc, t_required_item, &sv)) {
						log_error("door %d required_item invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					if (sv.len >= sizeof(out->doors[i].required_item)) {
						log_error("door %d required_item too long (max %zu)", i, sizeof(out->doors[i].required_item) - 1u);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					memcpy(out->doors[i].required_item, sv.data, sv.len);
					out->doors[i].required_item[sv.len] = '\0';
				}
				if (t_required_item_missing_message != -1) {
					StringView sv;
					if (!json_get_string(&doc, t_required_item_missing_message, &sv)) {
						log_error("door %d required_item_missing_message invalid", i);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					if (sv.len >= sizeof(out->doors[i].required_item_missing_message)) {
						log_error("door %d required_item_missing_message too long (max %zu)", i, sizeof(out->doors[i].required_item_missing_message) - 1u);
						json_doc_destroy(&doc);
						map_load_result_destroy(out);
						return false;
					}
					memcpy(out->doors[i].required_item_missing_message, sv.data, sv.len);
					out->doors[i].required_item_missing_message[sv.len] = '\0';
				}
			}
		}
	}

	// optional entity placements
	if (t_entities != -1) {
		if (!json_token_is_array(&doc, t_entities)) {
			log_error("entities must be an array");
			json_doc_destroy(&doc);
			map_load_result_destroy(out);
			return false;
		}
		int ecount = json_array_size(&doc, t_entities);
		if (ecount > 0) {
			out->entities = (MapEntityPlacement*)calloc((size_t)ecount, sizeof(MapEntityPlacement));
			if (!out->entities) {
				log_error("failed to allocate entities");
				json_doc_destroy(&doc);
				map_load_result_destroy(out);
				return false;
			}
			out->entity_count = ecount;
			for (int i = 0; i < ecount; i++) {
				int te = json_array_nth(&doc, t_entities, i);
				if (!json_token_is_object(&doc, te)) {
					log_error("entity %d must be an object", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				int tdef = -1, ttype = -1, tx = -1, ty = -1, tyaw = -1;
				(void)json_object_get(&doc, te, "def", &tdef);
				(void)json_object_get(&doc, te, "type", &ttype); // legacy field (pre-entity-system)
				if (!json_object_get(&doc, te, "x", &tx) || !json_object_get(&doc, te, "y", &ty)) {
					log_error("entity %d missing required fields (x,y)", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				(void)json_object_get(&doc, te, "yaw_deg", &tyaw);

				// Determine def name.
				StringView sv_def = {0};
				bool has_def = false;
				if (tdef != -1 && json_token_is_string(&doc, tdef)) {
					has_def = json_get_string(&doc, tdef, &sv_def);
				} else if (ttype != -1 && json_token_is_string(&doc, ttype)) {
					// Legacy mapping: map old "type" values to new entity def names.
					StringView sv_type;
					if (json_get_string(&doc, ttype, &sv_type)) {
						if (sv_type.len == 13 && strncmp(sv_type.data, "pickup_health", 13) == 0) {
							sv_def.data = "health_pickup";
							sv_def.len = 13;
							has_def = true;
						}
					}
				}
				if (!has_def) {
					// Unknown legacy type or missing def; keep entry but mark as inactive.
					out->entities[i].def_name[0] = '\0';
					out->entities[i].sector = -1;
					continue;
				}

				float x = 0.0f, y = 0.0f;
				if (!json_get_float(&doc, tx, &x) || !json_get_float(&doc, ty, &y)) {
					log_error("entity %d fields invalid", i);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
				float yaw_deg = 0.0f;
				if (tyaw != -1) {
					(void)json_get_float(&doc, tyaw, &yaw_deg);
				}
				size_t n = sv_def.len < 63 ? sv_def.len : 63;
				memcpy(out->entities[i].def_name, sv_def.data, n);
				out->entities[i].def_name[n] = '\0';
				out->entities[i].x = x;
				out->entities[i].y = y;
				out->entities[i].yaw_deg = yaw_deg;
				out->entities[i].sector = world_find_sector_at_point(&out->world, x, y);
				if (out->entities[i].sector < 0) {
					log_error("entity %d def '%s' is not inside any sector (x=%.3f y=%.3f)", i, out->entities[i].def_name, x, y);
					json_doc_destroy(&doc);
					map_load_result_destroy(out);
					return false;
				}
			}
		}
	}

	json_doc_destroy(&doc);

	if (!map_validate(&out->world, out->player_start_x, out->player_start_y, out->doors, out->door_count)) {
		map_load_result_destroy(out);
		return false;
	}

	(void)world_build_sector_wall_index(&out->world);

	return true;
}
