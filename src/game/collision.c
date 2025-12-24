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
	return w->back_sector < 0;
}

static float orient2(float ax, float ay, float bx, float by, float cx, float cy) {
	return (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
}

static bool on_segment2(float ax, float ay, float bx, float by, float px, float py) {
	float minx = ax < bx ? ax : bx;
	float maxx = ax > bx ? ax : bx;
	float miny = ay < by ? ay : by;
	float maxy = ay > by ? ay : by;
	const float eps = 1e-6f;
	return (px >= minx - eps && px <= maxx + eps && py >= miny - eps && py <= maxy + eps);
}

static bool segments_intersect2(float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy) {
	const float eps = 1e-6f;
	float o1 = orient2(ax, ay, bx, by, cx, cy);
	float o2 = orient2(ax, ay, bx, by, dx, dy);
	float o3 = orient2(cx, cy, dx, dy, ax, ay);
	float o4 = orient2(cx, cy, dx, dy, bx, by);

	// General case.
	if (((o1 > eps && o2 < -eps) || (o1 < -eps && o2 > eps)) && ((o3 > eps && o4 < -eps) || (o3 < -eps && o4 > eps))) {
		return true;
	}

	// Colinear/touching cases.
	if (fabsf(o1) <= eps && on_segment2(ax, ay, bx, by, cx, cy)) {
		return true;
	}
	if (fabsf(o2) <= eps && on_segment2(ax, ay, bx, by, dx, dy)) {
		return true;
	}
	if (fabsf(o3) <= eps && on_segment2(cx, cy, dx, dy, ax, ay)) {
		return true;
	}
	if (fabsf(o4) <= eps && on_segment2(cx, cy, dx, dy, bx, by)) {
		return true;
	}

	return false;
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
	float dx = to_x - from_x;
	float dy = to_y - from_y;
	if (dx * dx + dy * dy <= 1e-10f) {
		return true;
	}

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
		if (segments_intersect2(from_x, from_y, to_x, to_y, a.x, a.y, b.x, b.y)) {
			return false;
		}
	}
	return true;
}
