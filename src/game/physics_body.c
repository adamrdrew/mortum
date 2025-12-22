#include "game/physics_body.h"

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

static float cross2(float ax, float ay, float bx, float by) {
	return ax * by - ay * bx;
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

static bool wall_has_valid_vertices(const World* world, const Wall* w) {
	return world && w && w->v0 >= 0 && w->v0 < world->vertex_count && w->v1 >= 0 && w->v1 < world->vertex_count;
}

// Which sector is on the point's side of the wall.
// Assumes wall winding defines the half-plane for front/back.
static int wall_sector_for_point(const World* world, const Wall* w, float px, float py) {
	if (!world || !w) {
		return -1;
	}
	if (!wall_has_valid_vertices(world, w)) {
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

static bool body_fits_in_sector(const PhysicsBody* b, const Sector* s, float feet_z, const PhysicsBodyParams* params) {
	if (!b || !s || !params) {
		return false;
	}
	float z_max = s->ceil_z - params->headroom_epsilon;
	return (feet_z + b->height) <= z_max;
}

static bool wall_blocks_body(const World* world, const Wall* w, const PhysicsBody* body, float px, float py, const PhysicsBodyParams* params) {
	if (!world || !w || !body || !params) {
		return true;
	}
	if (w->back_sector < 0) {
		return true; // solid wall
	}

	int from_sector = wall_sector_for_point(world, w, px, py);
	if ((unsigned)from_sector >= (unsigned)world->sector_count) {
		return true;
	}
	int to_sector = (from_sector == w->front_sector) ? w->back_sector : w->front_sector;
	if ((unsigned)to_sector >= (unsigned)world->sector_count) {
		return true;
	}

	const Sector* from = &world->sectors[from_sector];
	const Sector* to = &world->sectors[to_sector];

	float step = to->floor_z - from->floor_z;
	if (step > body->step_height + 1e-6f) {
		return true;
	}

	// Headroom check in destination.
	// For step-up, the body will end up on the destination floor.
	float dest_feet_z = body->z;
	if (step > 1e-6f) {
		dest_feet_z = to->floor_z;
	}
	if (!body_fits_in_sector(body, to, dest_feet_z, params)) {
		return true;
	}

	return false; // portal is passable
}

static bool resolve_once(const World* world, const PhysicsBody* body, float* io_x, float* io_y, float* io_vx, float* io_vy, const PhysicsBodyParams* params) {
	bool any = false;
	float px = *io_x;
	float py = *io_y;

	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (!wall_blocks_body(world, w, body, px, py, params)) {
			continue;
		}
		if (!wall_has_valid_vertices(world, w)) {
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
		float r = body->radius;
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

static void move_resolved_2d(PhysicsBody* b, const World* world, float dx, float dy, const PhysicsBodyParams* params) {
	if (!b || !params) {
		return;
	}
	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		b->x += dx;
		b->y += dy;
		return;
	}

	float dist = sqrtf(dx * dx + dy * dy);
	float max_step = params->max_substep_dist;
	if (max_step <= 1e-6f) {
		max_step = b->radius * 0.5f;
		if (max_step < 0.02f) {
			max_step = 0.02f;
		}
	}
	int steps = 1;
	if (dist > max_step) {
		steps = (int)ceilf(dist / max_step);
	}
	if (steps < 1) {
		steps = 1;
	}
	if (steps > 64) {
		steps = 64; // keep bounded
	}

	float step_dx = dx / (float)steps;
	float step_dy = dy / (float)steps;

	for (int s = 0; s < steps; s++) {
		float x0 = b->x;
		float y0 = b->y;
		float to_x = x0 + step_dx;
		float to_y = y0 + step_dy;

		float vx = to_x - x0;
		float vy = to_y - y0;
		float x = to_x;
		float y = to_y;

		int iters = params->max_solve_iterations;
		if (iters < 1) {
			iters = 1;
		}
		if (iters > 16) {
			iters = 16;
		}

		for (int it = 0; it < iters; it++) {
			bool hit = resolve_once(world, b, &x, &y, &vx, &vy, params);
			if (!hit) {
				break;
			}
			x += vx;
			y += vy;
			vx = 0.0f;
			vy = 0.0f;
		}

		b->x = x;
		b->y = y;
	}
}

PhysicsBodyParams physics_body_params_default(void) {
	PhysicsBodyParams p;
	p.gravity_z = -18.0f;
	p.floor_epsilon = 1e-3f;
	p.headroom_epsilon = 0.08f;
	p.step_duration_s = 0.08f;
	p.max_substep_dist = 0.10f;
	p.max_solve_iterations = 4;
	return p;
}

void physics_body_init(PhysicsBody* b, float x, float y, float z, float radius, float height, float step_height) {
	if (!b) {
		return;
	}
	b->x = x;
	b->y = y;
	b->z = z;
	b->vx = 0.0f;
	b->vy = 0.0f;
	b->vz = 0.0f;
	b->radius = radius;
	b->height = height;
	b->step_height = step_height;
	b->on_ground = false;
	b->sector = -1;
	b->last_valid_sector = -1;
	b->step_up.active = false;
	b->step_up.start_z = z;
	b->step_up.target_z = z;
	b->step_up.t = 0.0f;
	b->step_up.duration_s = 0.0f;
	b->step_up.from_sector = -1;
	b->step_up.to_sector = -1;
	b->step_up.total_dx = 0.0f;
	b->step_up.total_dy = 0.0f;
	b->step_up.applied_frac = 0.0f;
}

static void update_sector(PhysicsBody* b, const World* world) {
	if (!b || !world) {
		return;
	}
	// If we already have a sector, keep it; movement transitions are handled via portal crossings.
	if ((unsigned)b->sector < (unsigned)world->sector_count) {
		b->last_valid_sector = b->sector;
		return;
	}
	int s = world_find_sector_at_point_stable(world, b->x, b->y, b->last_valid_sector);
	b->sector = s;
	if ((unsigned)s < (unsigned)world->sector_count) {
		b->last_valid_sector = s;
	}
}

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
	float denom = cross2(rx, ry, sx, sy);
	if (fabsf(denom) < 1e-8f) {
		return false;
	}
	float qpx = q0x - p0x;
	float qpy = q0y - p0y;
	float t = cross2(qpx, qpy, sx, sy) / denom;
	float u = cross2(qpx, qpy, rx, ry) / denom;
	if (t > 1e-6f && t <= 1.0f + 1e-6f && u >= -1e-6f && u <= 1.0f + 1e-6f) {
		if (out_t) {
			*out_t = t;
		}
		return true;
	}
	return false;
}

static int portal_other_sector(const Wall* w, int from_sector) {
	if (!w) {
		return -1;
	}
	return (from_sector == w->front_sector) ? w->back_sector : w->front_sector;
}

static bool find_first_portal_crossing(
	const World* world,
	const PhysicsBody* body,
	int from_sector,
	float x0,
	float y0,
	float x1,
	float y1,
	const PhysicsBodyParams* params,
	int* out_to_sector
) {
	if (!world || !body || !params || !out_to_sector) {
		return false;
	}
	if ((unsigned)from_sector >= (unsigned)world->sector_count) {
		return false;
	}
	float best_t = 1e30f;
	int best_to = -1;
	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (w->back_sector < 0) {
			continue;
		}
		if (w->front_sector != from_sector && w->back_sector != from_sector) {
			continue;
		}
		if (!wall_has_valid_vertices(world, w)) {
			continue;
		}
		// If this portal is blocked for this body, it can't be a valid transition.
		if (wall_blocks_body(world, w, body, x0, y0, params)) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		float t = 0.0f;
		if (!segment_intersect_param(x0, y0, x1, y1, a.x, a.y, b.x, b.y, &t)) {
			continue;
		}
		if (t < best_t) {
			best_t = t;
			best_to = portal_other_sector(w, from_sector);
		}
	}
	if ((unsigned)best_to < (unsigned)world->sector_count) {
		*out_to_sector = best_to;
		return true;
	}
	return false;
}

static void clamp_to_floor_and_ceiling(PhysicsBody* b, const World* world, const PhysicsBodyParams* params) {
	if (!b || !world || !params) {
		return;
	}
	if ((unsigned)b->sector >= (unsigned)world->sector_count) {
		return;
	}
	const Sector* s = &world->sectors[b->sector];

	float z_min = s->floor_z;
	float ceil_z = s->ceil_z;
	// During step-up, be conservative about ceiling clearance: require fitting under both ceilings.
	if (b->step_up.active) {
		if ((unsigned)b->step_up.from_sector < (unsigned)world->sector_count) {
			float c = world->sectors[b->step_up.from_sector].ceil_z;
			if (c < ceil_z) {
				ceil_z = c;
			}
		}
		if ((unsigned)b->step_up.to_sector < (unsigned)world->sector_count) {
			float c = world->sectors[b->step_up.to_sector].ceil_z;
			if (c < ceil_z) {
				ceil_z = c;
			}
		}
	}
	float z_max = (ceil_z - params->headroom_epsilon) - b->height;
	if (z_max < z_min) {
		z_max = z_min;
	}

	if (b->z < z_min) {
		b->z = z_min;
		b->vz = 0.0f;
		b->on_ground = true;
	}
	if (b->z > z_max) {
		b->z = z_max;
		b->vz = 0.0f;
	}

	if (fabsf(b->z - z_min) <= params->floor_epsilon && b->vz <= 0.0f) {
		b->z = z_min;
		b->vz = 0.0f;
		b->on_ground = true;
	}
}
static void start_step_up_preserve_xy(PhysicsBody* b, const World* world, int from_sector, int to_sector, float total_dx, float total_dy, const PhysicsBodyParams* params) {
	if (!b || !world || !params) {
		return;
	}
	if ((unsigned)from_sector >= (unsigned)world->sector_count) {
		return;
	}
	if ((unsigned)to_sector >= (unsigned)world->sector_count) {
		return;
	}
	const Sector* from = &world->sectors[from_sector];
	const Sector* to = &world->sectors[to_sector];
	float delta = to->floor_z - from->floor_z;
	if (delta <= 1e-6f) {
		return;
	}
	if (delta > b->step_height + 1e-6f) {
		return;
	}
	if (!body_fits_in_sector(b, to, to->floor_z, params)) {
		return;
	}

	b->step_up.active = true;
	b->step_up.start_z = b->z;
	b->step_up.target_z = to->floor_z;
	b->step_up.t = 0.0f;
	b->step_up.duration_s = params->step_duration_s;
	b->step_up.from_sector = from_sector;
	b->step_up.to_sector = to_sector;
	b->step_up.total_dx = total_dx;
	b->step_up.total_dy = total_dy;
	b->step_up.applied_frac = 0.0f;
	b->vz = 0.0f;
	b->on_ground = true;
}

static void handle_step_down_airborne_transition(PhysicsBody* b, const World* world, int old_sector, const PhysicsBodyParams* params) {
	if (!b || !world || !params) {
		return;
	}
	if ((unsigned)old_sector >= (unsigned)world->sector_count) {
		return;
	}
	if ((unsigned)b->sector >= (unsigned)world->sector_count) {
		return;
	}
	const Sector* from = &world->sectors[old_sector];
	const Sector* to = &world->sectors[b->sector];
	float delta = to->floor_z - from->floor_z;
	if (delta < -1e-6f) {
		// If we were effectively on the old floor, stepping down should not snap.
		if (fabsf(b->z - from->floor_z) <= params->floor_epsilon) {
			b->on_ground = false;
			b->vz = 0.0f;
		}
	}
}

static void physics_body_move_delta_internal(PhysicsBody* b, const World* world, float dx, float dy, const PhysicsBodyParams* params, bool allow_step_trigger, bool allow_while_stepping) {
	if (!b || !params) {
		return;
	}
	if (b->step_up.active && !allow_while_stepping) {
		// Non-interruptible: caller decides whether to advance horizontal motion while stepping.
		return;
	}

	int old_sector = b->sector;
	float old_x = b->x;
	float old_y = b->y;
	int old_sector_for_check = old_sector;
	if (world && (unsigned)old_sector_for_check >= (unsigned)world->sector_count) {
		old_sector_for_check = world_find_sector_at_point_stable(world, old_x, old_y, b->last_valid_sector);
	}

	// First, compute the resolved position we'd end up at.
	PhysicsBody tmp = *b;
	move_resolved_2d(&tmp, world, dx, dy, params);
	int to_sector = -1;
	bool crossed_portal = false;
	if (world && (unsigned)old_sector_for_check < (unsigned)world->sector_count) {
		crossed_portal = find_first_portal_crossing(world, b, old_sector_for_check, old_x, old_y, tmp.x, tmp.y, params, &to_sector);
	}

	// If this move would cross into a higher floor, do a pre-step: hold x/y, animate z up,
	// then apply the resolved horizontal movement after the step finishes.
	if (allow_step_trigger && crossed_portal && world && (unsigned)old_sector_for_check < (unsigned)world->sector_count && (unsigned)to_sector < (unsigned)world->sector_count) {
		float from_floor = world->sectors[old_sector_for_check].floor_z;
		float to_floor = world->sectors[to_sector].floor_z;
		float delta = to_floor - from_floor;
		if (delta > 1e-6f && delta <= b->step_height + 1e-6f) {
			start_step_up_preserve_xy(b, world, old_sector_for_check, to_sector, tmp.x - old_x, tmp.y - old_y, params);
			// Keep x/y in the lower sector for the duration of the step.
			b->x = old_x;
			b->y = old_y;
			b->sector = old_sector_for_check;
			b->last_valid_sector = old_sector_for_check;
			return;
		}
	}

	// No pre-step needed: commit resolved movement.
	b->x = tmp.x;
	b->y = tmp.y;
	if (world && crossed_portal && (unsigned)to_sector < (unsigned)world->sector_count) {
		b->sector = to_sector;
		b->last_valid_sector = to_sector;
	} else {
		// Keep current sector unless we didn't have one.
		if (world && (unsigned)old_sector_for_check < (unsigned)world->sector_count) {
			b->sector = old_sector_for_check;
			b->last_valid_sector = old_sector_for_check;
		} else {
			update_sector(b, world);
		}
	}
	if (world) {
		handle_step_down_airborne_transition(b, world, old_sector, params);
		clamp_to_floor_and_ceiling(b, world, params);
	}
}

void physics_body_move_delta(PhysicsBody* b, const World* world, float dx, float dy, const PhysicsBodyParams* params) {
	physics_body_move_delta_internal(b, world, dx, dy, params, true, false);
}

void physics_body_update(PhysicsBody* b, const World* world, float wish_vx, float wish_vy, double dt_s, const PhysicsBodyParams* params) {
	if (!b || !params) {
		return;
	}
	if (dt_s <= 0.0) {
		return;
	}

	// Ensure we have a sector on first update.
	update_sector(b, world);

	float x0 = b->x;
	float y0 = b->y;

	// If stepping up, run the animation first and (when complete) apply the stored horizontal move.
	if (b->step_up.active) {
		float dur = b->step_up.duration_s;
		if (dur <= 1e-4f) {
			dur = 1e-4f;
		}
		b->step_up.t += (float)dt_s;
		float a = clampf(b->step_up.t / dur, 0.0f, 1.0f);
		// Advance the stored horizontal movement progressively across the step.
		float da = a - b->step_up.applied_frac;
		if (da < 0.0f) {
			da = 0.0f;
		}
		if (da > 0.0f) {
			physics_body_move_delta_internal(b, world, b->step_up.total_dx * da, b->step_up.total_dy * da, params, false, true);
			// Keep sector locked to the origin until the step completes (prevents visual snap).
			if (world && (unsigned)b->step_up.from_sector < (unsigned)world->sector_count) {
				b->sector = b->step_up.from_sector;
				b->last_valid_sector = b->step_up.from_sector;
			}
		}
		b->step_up.applied_frac = a;

		b->z = b->step_up.start_z + (b->step_up.target_z - b->step_up.start_z) * a;
		b->vz = 0.0f;
		b->on_ground = true;
		if (a >= 1.0f - 1e-6f) {
			b->z = b->step_up.target_z;
			// Finish: switch to destination sector now that we're at its floor height.
			int to_sector = b->step_up.to_sector;
			b->step_up.active = false;
			b->step_up.total_dx = 0.0f;
			b->step_up.total_dy = 0.0f;
			b->step_up.applied_frac = 0.0f;
			b->step_up.from_sector = -1;
			b->step_up.to_sector = -1;
			if (world && (unsigned)to_sector < (unsigned)world->sector_count) {
				b->sector = to_sector;
				b->last_valid_sector = to_sector;
			}
		}
		clamp_to_floor_and_ceiling(b, world, params);
		b->vx = (b->x - x0) / (float)dt_s;
		b->vy = (b->y - y0) / (float)dt_s;
		return;
	}

	// Horizontal move.
	physics_body_move_delta_internal(b, world, wish_vx * (float)dt_s, wish_vy * (float)dt_s, params, true, false);
	b->vx = (b->x - x0) / (float)dt_s;
	b->vy = (b->y - y0) / (float)dt_s;

	// Vertical physics.
	if (world && (unsigned)b->sector < (unsigned)world->sector_count) {
		const Sector* s = &world->sectors[b->sector];
		float floor_z = s->floor_z;

		if (b->on_ground) {
			// Stay glued to floor unless the floor is below us (step-down) or we're pushed.
			if (b->z <= floor_z + params->floor_epsilon) {
				b->z = floor_z;
				b->vz = 0.0f;
			} else {
				b->on_ground = false;
			}
		}

		if (!b->on_ground) {
			b->vz += params->gravity_z * (float)dt_s;
			b->z += b->vz * (float)dt_s;
		}

		// Hard clamp to avoid ever going below floor.
		if (b->z < floor_z) {
			b->z = floor_z;
			b->vz = 0.0f;
			b->on_ground = true;
		}

		clamp_to_floor_and_ceiling(b, world, params);
	}
}
