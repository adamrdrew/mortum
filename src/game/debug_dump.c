#include "game/debug_dump.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static const char* entity_kind_str(EntityKind k) {
	switch (k) {
		case ENTITY_KIND_PICKUP: return "pickup";
		case ENTITY_KIND_PROJECTILE: return "projectile";
		case ENTITY_KIND_TURRET: return "turret";
		case ENTITY_KIND_ENEMY: return "enemy";
		case ENTITY_KIND_SUPPORT: return "support";
		default: return "invalid";
	}
}

static const char* entity_state_str(EntityState s) {
	switch (s) {
		case ENTITY_STATE_SPAWNING: return "spawning";
		case ENTITY_STATE_IDLE: return "idle";
		case ENTITY_STATE_ENGAGED: return "engaged";
		case ENTITY_STATE_ATTACK: return "attack";
		case ENTITY_STATE_DAMAGED: return "damaged";
		case ENTITY_STATE_DYING: return "dying";
		case ENTITY_STATE_DEAD: return "dead";
		default: return "(unknown)";
	}
}

static float deg_to_rad(float deg) {
	return deg * (float)M_PI / 180.0f;
}

static float cross2(float ax, float ay, float bx, float by) {
	return ax * by - ay * bx;
}

static float clampf(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static float camera_world_z_for_sector_approx2(const World* world, int sector, float z_offset) {
	// Keep in sync with render/raycast.c and entity sprite rendering.
	const float eye_height = 1.5f;
	const float headroom = 0.1f;
	if (!world || (unsigned)sector >= (unsigned)world->sector_count) {
		return eye_height + z_offset;
	}
	const Sector* s = &world->sectors[sector];
	float z = s->floor_z + eye_height + z_offset;
	float z_max = s->ceil_z - headroom;
	if (z > z_max) {
		z = z_max;
	}
	if (z < s->floor_z + headroom) {
		z = s->floor_z + headroom;
	}
	return z;
}

// Ray: o + t*d, segment: a + u*s
static bool ray_segment_hit(float ox, float oy, float dx, float dy, float ax, float ay, float bx, float by, float* out_t) {
	float sx = bx - ax;
	float sy = by - ay;
	float denom = cross2(dx, dy, sx, sy);
	if (fabsf(denom) < 1e-6f) {
		return false;
	}
	float aox = ax - ox;
	float aoy = ay - oy;
	float t = cross2(aox, aoy, sx, sy) / denom;
	float u = cross2(aox, aoy, dx, dy) / denom;
	if (t >= 0.0f && u >= 0.0f && u <= 1.0f) {
		*out_t = t;
		return true;
	}
	return false;
}

static float segment_u(float ax, float ay, float bx, float by, float px, float py) {
	float sx = bx - ax;
	float sy = by - ay;
	float len2 = sx * sx + sy * sy;
	if (len2 < 1e-8f) {
		return 0.0f;
	}
	float t = ((px - ax) * sx + (py - ay) * sy) / len2;
	return clampf(t, 0.0f, 1.0f);
}

// Matches the renderer's assumption: wall winding defines which side is front/back.
static int wall_sector_for_point(const World* world, const Wall* w, float px, float py) {
	if (!world || !w) {
		return -1;
	}
	if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
		return w->front_sector;
	}
	Vertex a = world->vertices[w->v0];
	Vertex b = world->vertices[w->v1];
	float ex = b.x - a.x;
	float ey = b.y - a.y;
	float apx = px - a.x;
	float apy = py - a.y;
	float side = cross2(ex, ey, apx, apy);

	int s0 = (side >= 0.0f) ? w->front_sector : w->back_sector;
	int s1 = (side >= 0.0f) ? w->back_sector : w->front_sector;

	if ((unsigned)s0 < (unsigned)world->sector_count) {
		return s0;
	}
	if ((unsigned)s1 < (unsigned)world->sector_count) {
		return s1;
	}
	return -1;
}

typedef struct RayHit {
	int wall_index;
	float t;
	float hit_x;
	float hit_y;
	float wall_u;
} RayHit;

static RayHit raycast_first_hit(const World* world, float ox, float oy, float dx, float dy) {
	RayHit r;
	memset(&r, 0, sizeof(r));
	r.wall_index = -1;
	r.t = 1e30f;
	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		return r;
	}
	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		float t = 0.0f;
		if (ray_segment_hit(ox, oy, dx, dy, a.x, a.y, b.x, b.y, &t)) {
			if (t < r.t) {
				r.t = t;
				r.wall_index = i;
			}
		}
	}
	if (r.wall_index >= 0) {
		r.hit_x = ox + dx * r.t;
		r.hit_y = oy + dy * r.t;
		const Wall* w = &world->walls[r.wall_index];
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		r.wall_u = segment_u(a.x, a.y, b.x, b.y, r.hit_x, r.hit_y);
	}
	return r;
}

static void print_sector(FILE* out, const World* world, int sector_index) {
	if (!out || !world) {
		return;
	}
	if ((unsigned)sector_index >= (unsigned)world->sector_count) {
		fprintf(out, "  sector_index: %d (invalid)\n", sector_index);
		return;
	}
	const Sector* s = &world->sectors[sector_index];
	fprintf(out, "  sector_index: %d\n", sector_index);
	fprintf(out, "  sector_id: %d\n", s->id);
	fprintf(out, "  floor_z: %.3f  ceil_z: %.3f\n", s->floor_z, s->ceil_z);
	fprintf(out, "  floor_tex: %s\n", s->floor_tex);
	fprintf(out, "  ceil_tex: %s\n", s->ceil_tex);
	fprintf(out, "  light: %.3f\n", s->light);
	fprintf(out, "  light_color: %.3f %.3f %.3f\n", s->light_color.r, s->light_color.g, s->light_color.b);
}

void debug_dump_print(FILE* out, const char* map_name, const World* world, const Player* player, const Camera* cam) {
	if (!out) {
		out = stdout;
	}
	fprintf(out, "\n=== MORTUM DEBUG DUMP ===\n");
	fprintf(out, "map: %s\n", map_name ? map_name : "(unknown)");
	if (!world) {
		fprintf(out, "world: (null)\n");
		fprintf(out, "=== END DEBUG DUMP ===\n");
		fflush(out);
		return;
	}
	fprintf(out, "world: vertices=%d walls=%d sectors=%d lights=%d\n", world->vertex_count, world->wall_count, world->sector_count, world->light_count);
	if (player) {
		fprintf(out, "player: x=%.4f y=%.4f z=%.4f angle_deg=%.3f\n", player->body.x, player->body.y, player->body.z, player->angle_deg);
		float ar = deg_to_rad(player->angle_deg);
		fprintf(out, "player_fwd: dx=%.6f dy=%.6f\n", cosf(ar), sinf(ar));

		// NOTE: In maps with overlapping/nested sectors (e.g. platform-in-room), a simple point-in-sector
		// query can be ambiguous and return the "wrong" containing sector. Gameplay and rendering should
		// prefer the PhysicsBody's tracked sector, which is updated via portal crossings.
		fprintf(out, "player_sector (authoritative):\n");
		print_sector(out, world, player->body.sector);

		int sec_point = world_find_sector_at_point(world, player->body.x, player->body.y);
		fprintf(out, "player_sector_guess_point:\n");
		print_sector(out, world, sec_point);

		int last_valid = player->body.sector;
		if ((unsigned)last_valid >= (unsigned)world->sector_count) {
			last_valid = -1;
		}
		int sec_stable = world_find_sector_at_point_stable(world, player->body.x, player->body.y, last_valid);
		fprintf(out, "player_sector_guess_stable:\n");
		print_sector(out, world, sec_stable);

		if (sec_point < 0 && sec_stable < 0 && (unsigned)player->body.sector >= (unsigned)world->sector_count) {
			fprintf(out, "  WARNING: player position is not inside any sector and player.body.sector is invalid.\n");
			fprintf(out, "  WARNING: this usually means the map has uncovered space at this position (or the player escaped geometry).\n");
			fprintf(out, "  WARNING: floor/ceiling selection may fall back to last-known sector and rays may hit none.\n");
		}
	}
	if (cam) {
		fprintf(out, "camera: x=%.4f y=%.4f angle_deg=%.3f fov_deg=%.3f\n", cam->x, cam->y, cam->angle_deg, cam->fov_deg);
	}

	// Ray samples (center/left/right) to help correlate what the renderer is hitting.
	if (cam) {
		float angles[3] = {
			cam->angle_deg - cam->fov_deg * 0.5f,
			cam->angle_deg,
			cam->angle_deg + cam->fov_deg * 0.5f,
		};
		const char* labels[3] = { "left", "center", "right" };
		for (int i = 0; i < 3; i++) {
			float rr = deg_to_rad(angles[i]);
			float dx = cosf(rr);
			float dy = sinf(rr);
			RayHit hit = raycast_first_hit(world, cam->x, cam->y, dx, dy);
			fprintf(out, "ray_%s: angle_deg=%.3f dir=(%.6f,%.6f)\n", labels[i], angles[i], dx, dy);
			if (hit.wall_index < 0) {
				fprintf(out, "  hit: none\n");
				continue;
			}
			const Wall* w = &world->walls[hit.wall_index];
			int view_sector = wall_sector_for_point(world, w, cam->x, cam->y);
			fprintf(out, "  hit: wall_index=%d t=%.6f hit=(%.6f,%.6f) wall_u=%.4f\n", hit.wall_index, hit.t, hit.hit_x, hit.hit_y, hit.wall_u);
			fprintf(out, "  wall: v0=%d v1=%d front_sector=%d back_sector=%d tex=%s\n", w->v0, w->v1, w->front_sector, w->back_sector, w->tex);
			fprintf(out, "  wall_view_sector_for_camera: %d\n", view_sector);
		}
	}

	fprintf(out, "=== END DEBUG DUMP ===\n");
	fflush(out);
}

void debug_dump_print_entities(
	FILE* out,
	const char* map_name,
	const World* world,
	const Player* player,
	const Camera* cam,
	const EntitySystem* entities,
	int fb_width,
	int fb_height,
	const float* wall_depth
) {
	if (!out) {
		out = stdout;
	}
	fprintf(out, "\n=== MORTUM ENTITY DUMP ===\n");
	fprintf(out, "map: %s\n", map_name ? map_name : "(unknown)");
	fprintf(out, "fb: %dx%d\n", fb_width, fb_height);

	if (!world || !cam || !entities || !entities->defs) {
		fprintf(out, "(missing world/camera/entities/defs)\n");
		fprintf(out, "=== END ENTITY DUMP ===\n");
		fflush(out);
		return;
	}

	if (player) {
		fprintf(
			out,
			"player: pos=(%.4f,%.4f,%.4f) sector=%d radius=%.3f on_ground=%d\n",
			player->body.x,
			player->body.y,
			player->body.z,
			player->body.sector,
			player->body.radius,
			player->body.on_ground ? 1 : 0
		);
	}

	float cam_rad = deg_to_rad(cam->angle_deg);
	float fx = cosf(cam_rad);
	float fy = sinf(cam_rad);
	float rx = -fy;
	float ry = fx;
	float fov_rad = deg_to_rad(cam->fov_deg);
	float half_w = 0.5f * (float)fb_width;
	float half_h = 0.5f * (float)fb_height;
	float tan_half_fov = tanf(0.5f * fov_rad);
	float focal = (tan_half_fov > 1e-4f) ? (half_w / tan_half_fov) : 0.0f;

	int start_sector = (player && (unsigned)player->body.sector < (unsigned)world->sector_count) ? player->body.sector : 0;
	float cam_z_world = camera_world_z_for_sector_approx2(world, start_sector, cam->z);

	fprintf(out, "camera: pos=(%.4f,%.4f) z_off=%.4f z_world=%.4f angle=%.3f fov=%.3f\n", cam->x, cam->y, cam->z, cam_z_world, cam->angle_deg, cam->fov_deg);
	fprintf(out, "camera_basis: fwd=(%.6f,%.6f) right=(%.6f,%.6f) tan_half_fov=%.6f focal_px=%.3f\n", fx, fy, rx, ry, tan_half_fov, focal);

	fprintf(out, "entities: alive=%u cap=%u defs=%u\n", (unsigned)entities->alive_count, (unsigned)entities->capacity, (unsigned)entities->defs->count);

	for (uint32_t i = 0; i < entities->capacity; i++) {
		if (!entities->alive[i]) {
			continue;
		}
		const Entity* e = &entities->entities[i];
		const EntityDef* def = &entities->defs->defs[e->def_id];
		fprintf(out, "\n- entity[%u]: id={%u,%u} def_id=%u def='%s' kind=%s\n", (unsigned)i, (unsigned)e->id.index, (unsigned)e->id.gen, (unsigned)e->def_id, def->name, entity_kind_str(def->kind));
		fprintf(out, "  state=%s state_time=%.3f hp=%d pending_despawn=%d yaw_deg=%.3f sprite_frame=%u\n", entity_state_str(e->state), e->state_time, e->hp, e->pending_despawn ? 1 : 0, e->yaw_deg, (unsigned)e->sprite_frame);
		fprintf(
			out,
			"  body: pos=(%.4f,%.4f,%.4f) vel=(%.4f,%.4f,%.4f) r=%.3f h=%.3f step_h=%.3f on_ground=%d sector=%d\n",
			e->body.x,
			e->body.y,
			e->body.z,
			e->body.vx,
			e->body.vy,
			e->body.vz,
			e->body.radius,
			e->body.height,
			e->body.step_height,
			e->body.on_ground ? 1 : 0,
			e->body.sector
		);
		if ((unsigned)e->body.sector < (unsigned)world->sector_count) {
			const Sector* s = &world->sectors[e->body.sector];
			fprintf(out, "  sector: idx=%d id=%d floor_z=%.3f ceil_z=%.3f\n", e->body.sector, s->id, s->floor_z, s->ceil_z);
		}
		fprintf(out, "  sprite: file='%s' file_wh=%dx%d frame_wh=%dx%d frames=%d scale=%.3f z_offset_px=%.3f\n",
			def->sprite.file.name,
			def->sprite.file.width,
			def->sprite.file.height,
			def->sprite.frames.width,
			def->sprite.frames.height,
			def->sprite.frames.count,
			def->sprite.scale,
			def->sprite.z_offset
		);

		// Projection diagnostics (match entities.c sprite rendering, but without texture sampling).
		float dx = e->body.x - cam->x;
		float dy = e->body.y - cam->y;
		float depth = dx * fx + dy * fy;
		float side = dx * rx + dy * ry;
		float dist2 = dx * dx + dy * dy;
		float dist = dist2 > 0.0f ? sqrtf(dist2) : 0.0f;
		float proj_depth = depth;
		const float min_proj_depth = 0.25f;
		int proj_clamped = 0;
		if (proj_depth < min_proj_depth) {
			proj_depth = min_proj_depth;
			proj_clamped = 1;
		}
		float ent_z = e->body.z + (def->sprite.z_offset / 64.0f);
		float scale = (proj_depth > 1e-6f) ? (focal / proj_depth) : 0.0f; // px per world unit

		int sheet_w = def->sprite.file.width;
		int sheet_h = def->sprite.file.height;
		int frame_w = def->sprite.frames.width > 0 ? def->sprite.frames.width : sheet_w;
		int frame_h = def->sprite.frames.height > 0 ? def->sprite.frames.height : sheet_h;
		float sprite_w_world = ((float)frame_w / 64.0f) * def->sprite.scale;
		float sprite_h_world = ((float)frame_h / 64.0f) * def->sprite.scale;
		// Match renderer: clamp projected size by clamping scale itself (uniformly).
		int max_w = fb_width * 2;
		int max_h = fb_height * 2;
		if (max_w > 0 && max_h > 0 && sprite_w_world > 1e-6f && sprite_h_world > 1e-6f) {
			float max_scale_w = (float)max_w / sprite_w_world;
			float max_scale_h = (float)max_h / sprite_h_world;
			float max_scale = max_scale_w < max_scale_h ? max_scale_w : max_scale_h;
			if (max_scale > 1e-6f && scale > max_scale) {
				scale = max_scale;
			}
		}

		float x_center = half_w + side * scale;
		float y_base = half_h + (cam_z_world - ent_z) * scale;
		float x_ndc = (tan_half_fov > 1e-6f && focal > 1e-6f) ? ((x_center - half_w) / half_w) : 0.0f;

		int sprite_w_px = (int)(sprite_w_world * scale + 0.5f);
		int sprite_h_px = (int)(sprite_h_world * scale + 0.5f);
		if (sprite_w_px < 2) {
			sprite_w_px = 2;
		}
		if (sprite_h_px < 2) {
			sprite_h_px = 2;
		}
		int x0 = (int)(x_center - 0.5f * (float)sprite_w_px);
		int x1 = x0 + sprite_w_px;
		int y1_raw = (int)y_base;
		int y1 = y1_raw;
		int y0 = y1 - sprite_h_px;

		const char* cull = "visible";
		if (depth <= 0.05f) {
			cull = "behind_or_too_close";
		} else if (x_ndc < -1.2f || x_ndc > 1.2f) {
			cull = "offscreen_x";
		} else if (sprite_w_px <= 1 || sprite_h_px <= 1) {
			cull = "too_small";
		}

		fprintf(out, "  proj: dx=%.4f dy=%.4f dist=%.4f depth=%.6f side=%.6f proj_depth=%.6f clamped=%d\n", dx, dy, dist, depth, side, proj_depth, proj_clamped);
		fprintf(out, "  proj: scale=%.6f x_ndc=%.6f x_center=%.3f y_base=%.3f y1_raw=%d y1=%d ent_z=%.4f cam_z_world=%.4f\n", scale, x_ndc, x_center, y_base, y1_raw, y1, ent_z, cam_z_world);
		fprintf(out, "  proj: sprite_px=%dx%d rect=[%d,%d]x[%d,%d] cull=%s\n", sprite_w_px, sprite_h_px, x0, x1, y0, y1, cull);

		if (wall_depth && fb_width > 0) {
			int sx = (int)(x_center + 0.5f);
			if (sx < 0) {
				sx = 0;
			}
			if (sx >= fb_width) {
				sx = fb_width - 1;
			}
			float wd = wall_depth[sx];
			fprintf(out, "  occlusion: wall_depth[x_center=%d]=%.6f depth=%.6f (%s)\n", sx, wd, depth, (depth >= wd) ? "occluded" : "in_front");
		}
	}

	fprintf(out, "\n=== END ENTITY DUMP ===\n");
	fflush(out);
}
