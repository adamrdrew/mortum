#include "game/collision.h"

#include <math.h>

static float clampf(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static float dot2(float ax, float ay, float bx, float by) {
	return ax * bx + ay * by;
}

static void closest_point_on_segment(float ax, float ay, float bx, float by, float px, float py, float* out_cx, float* out_cy) {
	float abx = bx - ax;
	float aby = by - ay;
	float apx = px - ax;
	float apy = py - ay;
	float ab_len2 = dot2(abx, aby, abx, aby);
	float t = 0.0f;
	if (ab_len2 > 1e-8f) {
		t = dot2(apx, apy, abx, aby) / ab_len2;
	}
	t = clampf(t, 0.0f, 1.0f);
	*out_cx = ax + abx * t;
	*out_cy = ay + aby * t;
}

static bool wall_is_solid(const Wall* w) {
	return w->back_sector < 0 || w->door_blocked;
}


static bool resolve_once(const World* world, float radius, float* io_x, float* io_y, float* io_vx, float* io_vy) {
	bool any = false;
	float px = *io_x;
	float py = *io_y;

	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (!wall_is_solid(w)) {
			continue;
		}
		if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		float cx = 0.0f;
		float cy = 0.0f;
		closest_point_on_segment(a.x, a.y, b.x, b.y, px, py, &cx, &cy);
		float dx = px - cx;
		float dy = py - cy;
		float dist2 = dx * dx + dy * dy;
		float r = radius;
		float r2 = r * r;
		if (dist2 < r2) {
			any = true;
			float dist = sqrtf(dist2);
			float nx = 0.0f;
			float ny = 0.0f;
			if (dist > 1e-6f) {
				nx = dx / dist;
				ny = dy / dist;
			} else {
				// Degenerate: pick a normal based on wall direction.
				float wx = b.x - a.x;
				float wy = b.y - a.y;
				float wl = sqrtf(wx * wx + wy * wy);
				if (wl > 1e-6f) {
					wx /= wl;
					wy /= wl;
					nx = -wy;
					ny = wx;
				} else {
					nx = 1.0f;
					ny = 0.0f;
				}
			}

			float push = r - dist;
			px += nx * push;
			py += ny * push;

			// Slide: remove normal component from remaining motion.
			float vn = dot2(*io_vx, *io_vy, nx, ny);
			if (vn < 0.0f) {
				*io_vx -= vn * nx;
				*io_vy -= vn * ny;
			}
		}
	}

	*io_x = px;
	*io_y = py;
	return any;
}

CollisionMoveResult collision_move_circle(const World* world, float radius, float from_x, float from_y, float to_x, float to_y) {
	CollisionMoveResult r;
	r.out_x = to_x;
	r.out_y = to_y;
	r.collided = false;
	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		return r;
	}

	float vx = to_x - from_x;
	float vy = to_y - from_y;
	float x = to_x;
	float y = to_y;

	// Small iterative solver: push out and slide a few times.
	for (int it = 0; it < 4; it++) {
		bool hit = resolve_once(world, radius, &x, &y, &vx, &vy);
		if (!hit) {
			break;
		}
		r.collided = true;
		// Re-apply remaining (tangential) motion.
		x += vx;
		y += vy;
		vx = 0.0f;
		vy = 0.0f;
	}

	r.out_x = x;
	r.out_y = y;
	return r;
}

bool collision_line_of_sight(const World* world, float from_x, float from_y, float to_x, float to_y) {
	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		return true;
	}
	// Zero-length segments trivially have LOS.
	float seg_dx = to_x - from_x;
	float seg_dy = to_y - from_y;
	float seg_len2 = seg_dx * seg_dx + seg_dy * seg_dy;
	if (seg_len2 <= 1e-10f) {
		return true;
	}

	// Proper segment intersection test against solid wall segments.
	// We ignore endpoint grazes to avoid LOS flicker when the line passes exactly through a vertex.
	const float eps = 1e-4f;
	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (!wall_is_solid(w)) {
			continue;
		}
		if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];

		float r_x = to_x - from_x;
		float r_y = to_y - from_y;
		float s_x = b.x - a.x;
		float s_y = b.y - a.y;
		float denom = r_x * s_y - r_y * s_x;
		if (fabsf(denom) <= 1e-10f) {
			continue; // parallel/colinear -> treat as non-blocking for LOS
		}
		float qpx = a.x - from_x;
		float qpy = a.y - from_y;
		float t = (qpx * s_y - qpy * s_x) / denom;
		float u = (qpx * r_y - qpy * r_x) / denom;
		if (t > eps && t < 1.0f - eps && u > eps && u < 1.0f - eps) {
			return false;
		}
	}
	return true;
}
