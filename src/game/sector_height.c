#include "game/sector_height.h"

#include "game/physics_body.h"
#include "game/tuning.h"

#include <math.h>
#include <string.h>

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

static int world_find_sector_index_by_id(const World* world, int sector_id) {
	if (!world) {
		return -1;
	}
	for (int i = 0; i < world->sector_count; i++) {
		if (world->sectors[i].id == sector_id) {
			return i;
		}
	}
	return -1;
}

static bool sector_is_at(float a, float b) {
	return fabsf(a - b) <= 1e-4f;
}

static bool any_sector_moving(const World* world) {
	if (!world) {
		return false;
	}
	for (int i = 0; i < world->sector_count; i++) {
		if (world->sectors[i].floor_moving) {
			return true;
		}
	}
	return false;
}

static void wall_set_current_tex(Wall* w, const char* tex) {
	if (!w || !tex) {
		return;
	}
	strncpy(w->tex, tex, sizeof(w->tex));
	w->tex[sizeof(w->tex) - 1] = '\0';
}

static void apply_wall_tex_for_sector_state(World* world, int wall_index, const Sector* s) {
	if (!world || !s || wall_index < 0 || wall_index >= world->wall_count) {
		return;
	}
	Wall* w = &world->walls[wall_index];
	if (!w->toggle_sector) {
		return;
	}
	if (sector_is_at(s->floor_z, s->floor_z_origin)) {
		wall_set_current_tex(w, w->base_tex);
		return;
	}
	if (sector_is_at(s->floor_z, s->floor_z_toggled_pos)) {
		if (w->active_tex[0] != '\0') {
			wall_set_current_tex(w, w->active_tex);
		} else {
			wall_set_current_tex(w, w->base_tex);
		}
		return;
	}
}

static bool sector_can_fit_body_at_floor(const Sector* s, const PhysicsBody* b, float floor_z, const PhysicsBodyParams* params) {
	if (!s || !b || !params) {
		return false;
	}
	float z_max = s->ceil_z - params->headroom_epsilon;
	return (floor_z + b->height) <= z_max;
}

static bool sector_step_is_safe_for_player(const Sector* s, const Player* player, float new_floor_z, float old_floor_z, float delta_floor, const PhysicsBodyParams* params) {
	if (!s || !player || !params) {
		return true;
	}
	const PhysicsBody* b = &player->body;
	(void)old_floor_z;

	float candidate_feet_z = b->z;
	// If the floor moves upward into the player, they will be pushed up to at least the floor.
	if (candidate_feet_z < new_floor_z) {
		candidate_feet_z = new_floor_z;
	}
	// If riding the platform, the player moves by the floor delta.
	if (b->on_ground && fabsf(b->z - old_floor_z) <= (params->floor_epsilon + 0.05f)) {
		candidate_feet_z = b->z + delta_floor;
		if (candidate_feet_z < new_floor_z) {
			candidate_feet_z = new_floor_z;
		}
	}
	return sector_can_fit_body_at_floor(s, b, candidate_feet_z, params);
}

bool sector_height_try_toggle_touching_wall(World* world, Player* player) {
	if (!world || !player) {
		return false;
	}
	if (!world->walls || world->wall_count <= 0 || world->vertex_count <= 0) {
		return false;
	}
	if (any_sector_moving(world)) {
		return false; // global lock while any sector is moving
	}
	if (player->noclip) {
		return false;
	}
	if ((unsigned)player->body.sector >= (unsigned)world->sector_count) {
		return false;
	}

	const float touch_eps = 0.03f;
	float best_dist2 = 1e30f;
	int best_wall = -1;

	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (!w->toggle_sector) {
			continue;
		}
		if (!wall_has_valid_vertices(world, w)) {
			continue;
		}
		int side_sector = wall_sector_for_point(world, w, player->body.x, player->body.y);
		if (side_sector != player->body.sector) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		float cx = 0.0f;
		float cy = 0.0f;
		closest_point_on_segment(a.x, a.y, b.x, b.y, player->body.x, player->body.y, &cx, &cy);
		float dx = player->body.x - cx;
		float dy = player->body.y - cy;
		float dist2 = dx * dx + dy * dy;
		float r = player->body.radius + touch_eps;
		if (dist2 > r * r) {
			continue;
		}
		if (dist2 < best_dist2) {
			best_dist2 = dist2;
			best_wall = i;
		}
	}

	if (best_wall < 0) {
		return false;
	}

	Wall* w = &world->walls[best_wall];
	int target_sector = -1;
	if (w->toggle_sector_id != -1) {
		target_sector = world_find_sector_index_by_id(world, w->toggle_sector_id);
	} else {
		target_sector = player->body.sector;
	}
	if ((unsigned)target_sector >= (unsigned)world->sector_count) {
		return false;
	}

	Sector* s = &world->sectors[target_sector];
	if (!s->movable) {
		return false;
	}
	if (s->floor_moving) {
		return false;
	}

	// One-shot bail: already at final position.
	if (w->toggle_sector_oneshot && sector_is_at(s->floor_z, s->floor_z_toggled_pos)) {
		return false;
	}

	float dest = s->floor_z_toggled_pos;
	if (sector_is_at(s->floor_z, s->floor_z_toggled_pos)) {
		dest = s->floor_z_origin;
	} else if (sector_is_at(s->floor_z, s->floor_z_origin)) {
		dest = s->floor_z_toggled_pos;
	} else {
		// Not at an endpoint; we don't allow retrigger mid-motion.
		return false;
	}

	// Start move.
	s->floor_z_target = dest;
	s->floor_moving = true;
	s->floor_toggle_wall_index = best_wall;
	return true;
}

void sector_height_update(World* world, Player* player, double dt_s) {
	if (!world || dt_s <= 0.0) {
		return;
	}
	const PhysicsBodyParams params = physics_body_params_default();
	const float speed = MORTUM_SECTOR_FLOOR_SPEED;
	if (speed <= 1e-6f) {
		return;
	}

	for (int i = 0; i < world->sector_count; i++) {
		Sector* s = &world->sectors[i];
		if (!s->floor_moving) {
			continue;
		}

		float old_floor = s->floor_z;
		float target = s->floor_z_target;
		float diff = target - old_floor;
		if (fabsf(diff) <= 1e-4f) {
			s->floor_z = target;
			s->floor_moving = false;
			apply_wall_tex_for_sector_state(world, s->floor_toggle_wall_index, s);
			s->floor_toggle_wall_index = -1;
			continue;
		}

		float step = (float)(speed * dt_s);
		float delta = (diff > 0.0f) ? fminf(diff, step) : fmaxf(diff, -step);
		float new_floor = old_floor + delta;

		// Ceiling safety: if moving up would crush the player, cancel movement.
		if (player && (unsigned)player->body.sector == (unsigned)i && !player->noclip) {
			if (!sector_step_is_safe_for_player(s, player, new_floor, old_floor, delta, &params)) {
				s->floor_moving = false;
				s->floor_z_target = s->floor_z; // freeze
				s->floor_toggle_wall_index = -1;
				continue;
			}
		}

		s->floor_z = new_floor;

		// Ride logic: keep the player glued to the moving floor when on it.
		if (player && (unsigned)player->body.sector == (unsigned)i && !player->noclip) {
			if (player->body.on_ground && fabsf(player->body.z - old_floor) <= (params.floor_epsilon + 0.05f)) {
				player->body.z += delta;
			}
			// Never allow ending up below the floor after the move.
			if (player->body.z < s->floor_z) {
				player->body.z = s->floor_z;
			}
		}

		if (sector_is_at(s->floor_z, s->floor_z_target)) {
			s->floor_z = s->floor_z_target;
			s->floor_moving = false;
			apply_wall_tex_for_sector_state(world, s->floor_toggle_wall_index, s);
			s->floor_toggle_wall_index = -1;
		}
	}
}
