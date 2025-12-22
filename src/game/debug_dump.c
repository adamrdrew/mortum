#include "game/debug_dump.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

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

		int sec = world_find_sector_at_point(world, player->body.x, player->body.y);
		fprintf(out, "player_sector_guess:\n");
		print_sector(out, world, sec);
		if (sec < 0) {
			fprintf(out, "  WARNING: player position is not inside any sector.\n");
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
