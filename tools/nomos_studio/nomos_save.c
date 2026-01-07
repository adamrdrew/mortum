// Nomos Studio - Map saving implementation
#include "nomos_save.h"

#include "game/world.h"
#include "render/lighting.h"
#include "game/particle_emitters.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

// Helper macros for clean JSON writing
#define JSON_INDENT(fp, depth) do { for (int _i = 0; _i < (depth); _i++) fprintf(fp, "  "); } while (0)

static void write_string(FILE* fp, const char* str) {
	fputc('"', fp);
	for (const char* p = str; *p; p++) {
		if (*p == '"') fprintf(fp, "\\\"");
		else if (*p == '\\') fprintf(fp, "\\\\");
		else if (*p == '\n') fprintf(fp, "\\n");
		else if (*p == '\r') fprintf(fp, "\\r");
		else if (*p == '\t') fprintf(fp, "\\t");
		else fputc(*p, fp);
	}
	fputc('"', fp);
}

static void write_float(FILE* fp, float val) {
	// Output nicer format for round numbers
	if (floorf(val) == val && fabsf(val) < 1000000.0f) {
		fprintf(fp, "%.1f", val);
	} else {
		fprintf(fp, "%.6g", val);
	}
}

static void write_color(FILE* fp, LightColor c) {
	fprintf(fp, "{\"r\": ");
	write_float(fp, c.r);
	fprintf(fp, ", \"g\": ");
	write_float(fp, c.g);
	fprintf(fp, ", \"b\": ");
	write_float(fp, c.b);
	fprintf(fp, "}");
}

static void write_particle_color(FILE* fp, ParticleEmitterColor c) {
	fprintf(fp, "{\"r\": ");
	write_float(fp, c.r);
	fprintf(fp, ", \"g\": ");
	write_float(fp, c.g);
	fprintf(fp, ", \"b\": ");
	write_float(fp, c.b);
	if (c.opacity != 1.0f) {
		fprintf(fp, ", \"opacity\": ");
		write_float(fp, c.opacity);
	}
	fprintf(fp, "}");
}

bool nomos_save_map(const MapLoadResult* map, const char* filepath) {
	if (!map || !filepath) return false;
	
	FILE* fp = fopen(filepath, "w");
	if (!fp) return false;
	
	const World* world = &map->world;
	
	fprintf(fp, "{\n");
	
	// Version
	JSON_INDENT(fp, 1);
	fprintf(fp, "\"version\": 1,\n");
	
	// Background music
	if (map->bgmusic[0]) {
		JSON_INDENT(fp, 1);
		fprintf(fp, "\"bgmusic\": ");
		write_string(fp, map->bgmusic);
		fprintf(fp, ",\n");
	}
	
	// Soundfont
	if (map->soundfont[0]) {
		JSON_INDENT(fp, 1);
		fprintf(fp, "\"soundfont\": ");
		write_string(fp, map->soundfont);
		fprintf(fp, ",\n");
	}
	
	// Sky
	if (map->sky[0]) {
		JSON_INDENT(fp, 1);
		fprintf(fp, "\"sky\": ");
		write_string(fp, map->sky);
		fprintf(fp, ",\n");
	}
	
	// Player start
	JSON_INDENT(fp, 1);
	fprintf(fp, "\"player_start\": {\"x\": ");
	write_float(fp, map->player_start_x);
	fprintf(fp, ", \"y\": ");
	write_float(fp, map->player_start_y);
	fprintf(fp, ", \"angle_deg\": ");
	write_float(fp, map->player_start_angle_deg);
	fprintf(fp, "},\n");
	
	// Vertices
	JSON_INDENT(fp, 1);
	fprintf(fp, "\"vertices\": [\n");
	for (int i = 0; i < world->vertex_count; i++) {
		JSON_INDENT(fp, 2);
		fprintf(fp, "{\"x\": ");
		write_float(fp, world->vertices[i].x);
		fprintf(fp, ", \"y\": ");
		write_float(fp, world->vertices[i].y);
		fprintf(fp, "}");
		if (i < world->vertex_count - 1) fprintf(fp, ",");
		fprintf(fp, "\n");
	}
	JSON_INDENT(fp, 1);
	fprintf(fp, "],\n");
	
	// Sectors
	JSON_INDENT(fp, 1);
	fprintf(fp, "\"sectors\": [\n");
	for (int i = 0; i < world->sector_count; i++) {
		Sector* s = &world->sectors[i];
		JSON_INDENT(fp, 2);
		fprintf(fp, "{\"id\": %d", s->id);
		fprintf(fp, ", \"floor_z\": ");
		write_float(fp, s->floor_z_origin); // Use origin for authored value
		fprintf(fp, ", \"ceil_z\": ");
		write_float(fp, s->ceil_z);
		fprintf(fp, ", \"floor_tex\": ");
		write_string(fp, s->floor_tex);
		fprintf(fp, ", \"ceil_tex\": ");
		write_string(fp, s->ceil_tex);
		fprintf(fp, ", \"light\": ");
		write_float(fp, s->light);
		
		// Light color (optional, only if not white)
		if (s->light_color.r != 1.0f || s->light_color.g != 1.0f || s->light_color.b != 1.0f) {
			fprintf(fp, ", \"light_color\": ");
			write_color(fp, s->light_color);
		}
		
		// Movable sector (optional)
		if (s->movable) {
			fprintf(fp, ", \"movable\": true, \"floor_z_toggled_pos\": ");
			write_float(fp, s->floor_z_toggled_pos);
		}
		
		fprintf(fp, "}");
		if (i < world->sector_count - 1) fprintf(fp, ",");
		fprintf(fp, "\n");
	}
	JSON_INDENT(fp, 1);
	fprintf(fp, "],\n");
	
	// Walls
	JSON_INDENT(fp, 1);
	fprintf(fp, "\"walls\": [\n");
	for (int i = 0; i < world->wall_count; i++) {
		Wall* w = &world->walls[i];
		JSON_INDENT(fp, 2);
		fprintf(fp, "{\"v0\": %d, \"v1\": %d", w->v0, w->v1);
		fprintf(fp, ", \"front_sector\": %d, \"back_sector\": %d", w->front_sector, w->back_sector);
		
		// Use base_tex as the authored texture
		const char* tex = w->base_tex[0] ? w->base_tex : w->tex;
		fprintf(fp, ", \"tex\": ");
		write_string(fp, tex);
		
		// Optional fields
		if (w->active_tex[0]) {
			fprintf(fp, ", \"active_tex\": ");
			write_string(fp, w->active_tex);
		}
		if (w->end_level) {
			fprintf(fp, ", \"end_level\": true");
		}
		if (w->toggle_sector) {
			fprintf(fp, ", \"toggle_sector\": true");
			if (w->toggle_sector_id >= 0) {
				fprintf(fp, ", \"toggle_sector_id\": %d", w->toggle_sector_id);
			}
			if (w->toggle_sector_oneshot) {
				fprintf(fp, ", \"toggle_sector_oneshot\": true");
			}
		}
		if (w->required_item[0]) {
			fprintf(fp, ", \"required_item\": ");
			write_string(fp, w->required_item);
		}
		if (w->required_item_missing_message[0]) {
			fprintf(fp, ", \"required_item_missing_message\": ");
			write_string(fp, w->required_item_missing_message);
		}
		if (w->toggle_sound[0]) {
			fprintf(fp, ", \"toggle_sound\": ");
			write_string(fp, w->toggle_sound);
		}
		if (w->toggle_sound_finish[0]) {
			fprintf(fp, ", \"toggle_sound_finish\": ");
			write_string(fp, w->toggle_sound_finish);
		}
		
		fprintf(fp, "}");
		if (i < world->wall_count - 1) fprintf(fp, ",");
		fprintf(fp, "\n");
	}
	JSON_INDENT(fp, 1);
	fprintf(fp, "],\n");
	
	// Lights
	if (world->light_count > 0) {
		JSON_INDENT(fp, 1);
		fprintf(fp, "\"lights\": [\n");
		bool first = true;
		for (int i = 0; i < world->light_count; i++) {
			if (world->light_alive && !world->light_alive[i]) continue;
			PointLight* l = &world->lights[i];
			if (!first) fprintf(fp, ",\n");
			first = false;
			JSON_INDENT(fp, 2);
			fprintf(fp, "{\"x\": ");
			write_float(fp, l->x);
			fprintf(fp, ", \"y\": ");
			write_float(fp, l->y);
			if (l->z != 0.0f) {
				fprintf(fp, ", \"z\": ");
				write_float(fp, l->z);
			}
			fprintf(fp, ", \"radius\": ");
			write_float(fp, l->radius);
			fprintf(fp, ", \"intensity\": ");
			write_float(fp, l->intensity);
			if (l->color.r != 1.0f || l->color.g != 1.0f || l->color.b != 1.0f) {
				fprintf(fp, ", \"color\": ");
				write_color(fp, l->color);
			}
			if (l->flicker != LIGHT_FLICKER_NONE) {
				const char* flicker_name = "none";
				switch (l->flicker) {
					case LIGHT_FLICKER_FLAME: flicker_name = "flame"; break;
					case LIGHT_FLICKER_MALFUNCTION: flicker_name = "malfunction"; break;
					default: break;
				}
				fprintf(fp, ", \"flicker\": ");
				write_string(fp, flicker_name);
			}
			fprintf(fp, "}");
		}
		fprintf(fp, "\n");
		JSON_INDENT(fp, 1);
		fprintf(fp, "],\n");
	}
	
	// Doors
	if (map->door_count > 0) {
		JSON_INDENT(fp, 1);
		fprintf(fp, "\"doors\": [\n");
		for (int i = 0; i < map->door_count; i++) {
			const MapDoor* d = &map->doors[i];
			JSON_INDENT(fp, 2);
			fprintf(fp, "{\"id\": ");
			write_string(fp, d->id);
			fprintf(fp, ", \"wall_index\": %d", d->wall_index);
			fprintf(fp, ", \"tex\": ");
			write_string(fp, d->tex);
			if (d->starts_closed) {
				fprintf(fp, ", \"starts_closed\": true");
			}
			if (d->sound_open[0]) {
				fprintf(fp, ", \"sound_open\": ");
				write_string(fp, d->sound_open);
			}
			if (d->required_item[0]) {
				fprintf(fp, ", \"required_item\": ");
				write_string(fp, d->required_item);
			}
			if (d->required_item_missing_message[0]) {
				fprintf(fp, ", \"required_item_missing_message\": ");
				write_string(fp, d->required_item_missing_message);
			}
			fprintf(fp, "}");
			if (i < map->door_count - 1) fprintf(fp, ",");
			fprintf(fp, "\n");
		}
		JSON_INDENT(fp, 1);
		fprintf(fp, "],\n");
	}
	
	// Sound emitters
	if (map->sound_count > 0) {
		JSON_INDENT(fp, 1);
		fprintf(fp, "\"sounds\": [\n");
		for (int i = 0; i < map->sound_count; i++) {
			const MapSoundEmitter* s = &map->sounds[i];
			JSON_INDENT(fp, 2);
			fprintf(fp, "{\"x\": ");
			write_float(fp, s->x);
			fprintf(fp, ", \"y\": ");
			write_float(fp, s->y);
			fprintf(fp, ", \"sound\": ");
			write_string(fp, s->sound);
			if (s->loop) fprintf(fp, ", \"loop\": true");
			if (s->spatial) fprintf(fp, ", \"spatial\": true");
			if (s->gain != 1.0f) {
				fprintf(fp, ", \"gain\": ");
				write_float(fp, s->gain);
			}
			fprintf(fp, "}");
			if (i < map->sound_count - 1) fprintf(fp, ",");
			fprintf(fp, "\n");
		}
		JSON_INDENT(fp, 1);
		fprintf(fp, "],\n");
	}
	
	// Particle emitters
	if (map->particle_count > 0) {
		JSON_INDENT(fp, 1);
		fprintf(fp, "\"particles\": [\n");
		for (int i = 0; i < map->particle_count; i++) {
			const MapParticleEmitter* p = &map->particles[i];
			JSON_INDENT(fp, 2);
			fprintf(fp, "{\"x\": ");
			write_float(fp, p->x);
			fprintf(fp, ", \"y\": ");
			write_float(fp, p->y);
			if (p->z != 0.0f) {
				fprintf(fp, ", \"z\": ");
				write_float(fp, p->z);
			}
			
			// Emit definition inline
			fprintf(fp, ", \"particle_life_ms\": %d", p->def.particle_life_ms);
			fprintf(fp, ", \"emit_interval_ms\": %d", p->def.emit_interval_ms);
			
			if (p->def.offset_jitter != 0.0f) {
				fprintf(fp, ", \"offset_jitter\": ");
				write_float(fp, p->def.offset_jitter);
			}
			
			if (p->def.image[0]) {
				fprintf(fp, ", \"image\": ");
				write_string(fp, p->def.image);
			} else {
				const char* shape_name = "square";
				switch (p->def.shape) {
					case PARTICLE_SHAPE_CIRCLE: shape_name = "circle"; break;
					case PARTICLE_SHAPE_SQUARE: shape_name = "square"; break;
					default: break;
				}
				fprintf(fp, ", \"shape\": ");
				write_string(fp, shape_name);
			}
			
			// Start keyframe
			fprintf(fp, ", \"start\": {");
			fprintf(fp, "\"opacity\": ");
			write_float(fp, p->def.start.opacity);
			fprintf(fp, ", \"color\": ");
			write_particle_color(fp, p->def.start.color);
			fprintf(fp, ", \"size\": ");
			write_float(fp, p->def.start.size);
			if (p->def.start.offset.x != 0 || p->def.start.offset.y != 0 || p->def.start.offset.z != 0) {
				fprintf(fp, ", \"offset\": {\"x\": ");
				write_float(fp, p->def.start.offset.x);
				fprintf(fp, ", \"y\": ");
				write_float(fp, p->def.start.offset.y);
				fprintf(fp, ", \"z\": ");
				write_float(fp, p->def.start.offset.z);
				fprintf(fp, "}");
			}
			fprintf(fp, "}");
			
			// End keyframe
			fprintf(fp, ", \"end\": {");
			fprintf(fp, "\"opacity\": ");
			write_float(fp, p->def.end.opacity);
			fprintf(fp, ", \"color\": ");
			write_particle_color(fp, p->def.end.color);
			fprintf(fp, ", \"size\": ");
			write_float(fp, p->def.end.size);
			if (p->def.end.offset.x != 0 || p->def.end.offset.y != 0 || p->def.end.offset.z != 0) {
				fprintf(fp, ", \"offset\": {\"x\": ");
				write_float(fp, p->def.end.offset.x);
				fprintf(fp, ", \"y\": ");
				write_float(fp, p->def.end.offset.y);
				fprintf(fp, ", \"z\": ");
				write_float(fp, p->def.end.offset.z);
				fprintf(fp, "}");
			}
			fprintf(fp, "}");
			
			// Rotation
			if (p->def.rotate.enabled) {
				fprintf(fp, ", \"rotate\": {\"tick\": {\"deg\": ");
				write_float(fp, p->def.rotate.tick.deg);
				fprintf(fp, ", \"time_ms\": %d}}", p->def.rotate.tick.time_ms);
			}
			
			fprintf(fp, "}");
			if (i < map->particle_count - 1) fprintf(fp, ",");
			fprintf(fp, "\n");
		}
		JSON_INDENT(fp, 1);
		fprintf(fp, "],\n");
	}
	
	// Entities (last, no trailing comma)
	JSON_INDENT(fp, 1);
	fprintf(fp, "\"entities\": [\n");
	for (int i = 0; i < map->entity_count; i++) {
		const MapEntityPlacement* e = &map->entities[i];
		JSON_INDENT(fp, 2);
		fprintf(fp, "{\"def\": ");
		write_string(fp, e->def_name);
		fprintf(fp, ", \"x\": ");
		write_float(fp, e->x);
		fprintf(fp, ", \"y\": ");
		write_float(fp, e->y);
		if (e->yaw_deg != 0.0f) {
			fprintf(fp, ", \"yaw_deg\": ");
			write_float(fp, e->yaw_deg);
		}
		fprintf(fp, "}");
		if (i < map->entity_count - 1) fprintf(fp, ",");
		fprintf(fp, "\n");
	}
	JSON_INDENT(fp, 1);
	fprintf(fp, "]\n");
	
	fprintf(fp, "}\n");
	
	fclose(fp);
	return true;
}
