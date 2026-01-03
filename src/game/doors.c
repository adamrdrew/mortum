#include "game/doors.h"

#include "game/inventory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Visual door raise duration (seconds). Fast but readable.
#define DOOR_OPEN_DURATION_S 0.55f

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

static bool wall_has_valid_vertices(const World* world, const Wall* w) {
	return world && w && w->v0 >= 0 && w->v0 < world->vertex_count && w->v1 >= 0 && w->v1 < world->vertex_count;
}

static int find_twin_portal_wall_index(const World* world, int wall_index) {
	if (!world || wall_index < 0 || wall_index >= world->wall_count) {
		return -1;
	}
	const Wall* w = &world->walls[wall_index];
	if (w->back_sector == -1) {
		return -1;
	}
	// Twin wall is the opposite-directed portal edge between the same two sectors.
	for (int i = 0; i < world->wall_count; i++) {
		if (i == wall_index) {
			continue;
		}
		const Wall* o = &world->walls[i];
		if (o->v0 == w->v1 && o->v1 == w->v0 && o->front_sector == w->back_sector && o->back_sector == w->front_sector) {
			return i;
		}
	}
	return -1;
}

static void door_apply_wall_state(World* world, int wall_index, bool blocked, float open_t, const char* tex_override) {
	if (!world || wall_index < 0 || wall_index >= world->wall_count) {
		return;
	}
	int twin = find_twin_portal_wall_index(world, wall_index);
	Wall* w = &world->walls[wall_index];
	Wall* wt = (twin >= 0) ? &world->walls[twin] : NULL;

	w->door_blocked = blocked;
	w->door_open_t = open_t;
	if (wt) {
		wt->door_blocked = blocked;
		wt->door_open_t = open_t;
	}

	if (tex_override && tex_override[0] != '\0') {
		strncpy(w->tex, tex_override, sizeof(w->tex) - 1u);
		w->tex[sizeof(w->tex) - 1u] = '\0';
		if (wt) {
			strncpy(wt->tex, tex_override, sizeof(wt->tex) - 1u);
			wt->tex[sizeof(wt->tex) - 1u] = '\0';
		}
	}
}

static float player_dist2_to_wall_segment(const World* world, const Wall* w, float px, float py) {
	if (!wall_has_valid_vertices(world, w)) {
		return 1e30f;
	}
	Vertex a = world->vertices[w->v0];
	Vertex b = world->vertices[w->v1];
	float cx = 0.0f;
	float cy = 0.0f;
	closest_point_on_segment(a.x, a.y, b.x, b.y, px, py, &cx, &cy);
	float dx = px - cx;
	float dy = py - cy;
	return dx * dx + dy * dy;
}

void doors_init(Doors* self) {
	if (!self) {
		return;
	}
	self->doors = NULL;
	self->door_count = 0;
}

void doors_destroy(Doors* self) {
	if (!self) {
		return;
	}
	free(self->doors);
	self->doors = NULL;
	self->door_count = 0;
}

static void safe_copy(char* dst, size_t dst_cap, const char* src) {
	if (!dst || dst_cap == 0) {
		return;
	}
	if (!src) {
		dst[0] = '\0';
		return;
	}
	strncpy(dst, src, dst_cap - 1u);
	dst[dst_cap - 1u] = '\0';
}

bool doors_build_from_map(Doors* self, World* world, const MapDoor* defs, int def_count) {
	if (!self || !world) {
		return false;
	}
	doors_destroy(self);
	if (!defs || def_count <= 0) {
		return true;
	}
	Door* arr = (Door*)calloc((size_t)def_count, sizeof(Door));
	if (!arr) {
		return false;
	}
	self->doors = arr;
	self->door_count = def_count;

	for (int i = 0; i < def_count; i++) {
		const MapDoor* d = &defs[i];
		Door* out = &self->doors[i];
		safe_copy(out->id, sizeof(out->id), d->id);
		out->wall_index = d->wall_index;
		out->is_open = !d->starts_closed;
		out->is_opening = false;
		out->open_start_s = 0.0f;
		safe_copy(out->closed_tex, sizeof(out->closed_tex), d->tex);
		safe_copy(out->sound_open, sizeof(out->sound_open), d->sound_open);
		safe_copy(out->required_item, sizeof(out->required_item), d->required_item);
		safe_copy(out->required_item_missing_message, sizeof(out->required_item_missing_message), d->required_item_missing_message);
		out->next_allowed_s = 0.0f;
		out->next_deny_toast_s = 0.0f;

		// Apply initial wall state.
		if (!out->is_open) {
			door_apply_wall_state(world, out->wall_index, true, 0.0f, out->closed_tex);
		} else {
			door_apply_wall_state(world, out->wall_index, false, 1.0f, NULL);
		}
	}

	return true;
}

int doors_count(const Doors* self) {
	return self ? self->door_count : 0;
}

const char* doors_id_at(const Doors* self, int index) {
	if (!self || index < 0 || index >= self->door_count) {
		return NULL;
	}
	return self->doors[index].id;
}

static DoorsOpenResult door_try_open_index(
	Doors* self,
	World* world,
	const Player* player,
	Notifications* notifications,
	SoundEmitters* sfx,
	float listener_x,
	float listener_y,
	float now_s,
	int door_index
) {
	if (!self || !world || !player || door_index < 0 || door_index >= self->door_count) {
		return DOORS_INVALID;
	}
	Door* d = &self->doors[door_index];
	if (d->is_open) {
		return DOORS_ALREADY_OPEN;
	}
	if (d->is_opening) {
		return DOORS_ON_COOLDOWN;
	}
	if (now_s < d->next_allowed_s) {
		return DOORS_ON_COOLDOWN;
	}
	if (d->required_item[0] != '\0' && !inventory_contains(&player->inventory, d->required_item)) {
		const float deny_toast_cooldown_s = 0.75f;
		if (notifications && now_s >= d->next_deny_toast_s) {
			if (d->required_item_missing_message[0] != '\0') {
				(void)notifications_push_text(notifications, d->required_item_missing_message);
			} else {
				char msg[192];
				(void)snprintf(msg, sizeof(msg), "Missing required item: %s", d->required_item);
				msg[sizeof(msg) - 1] = '\0';
				(void)notifications_push_text(notifications, msg);
			}
			d->next_deny_toast_s = now_s + deny_toast_cooldown_s;
		}
		return DOORS_MISSING_REQUIRED_ITEM;
	}
	if (d->wall_index < 0 || d->wall_index >= world->wall_count) {
		return DOORS_INVALID;
	}

	// Start opening animation (keeps blocking until completion).
	d->is_opening = true;
	d->open_start_s = now_s;
	// Prevent repeated attempts while opening.
	d->next_allowed_s = now_s + DOOR_OPEN_DURATION_S;
	// Ensure door is in a consistent closed state at animation start.
	door_apply_wall_state(world, d->wall_index, true, 0.0f, d->closed_tex);

	if (sfx && d->sound_open[0] != '\0') {
		// Play from door midpoint.
		const Wall* w = &world->walls[d->wall_index];
		if (wall_has_valid_vertices(world, w)) {
			Vertex a = world->vertices[w->v0];
			Vertex b = world->vertices[w->v1];
			float wx = (a.x + b.x) * 0.5f;
			float wy = (a.y + b.y) * 0.5f;
			sound_emitters_play_one_shot_at(sfx, d->sound_open, wx, wy, true, 1.0f, listener_x, listener_y);
		}
	}

	return DOORS_OPENED;
}

void doors_update(Doors* self, World* world, float now_s) {
	if (!self || !world) {
		return;
	}
	for (int i = 0; i < self->door_count; i++) {
		Door* d = &self->doors[i];
		if (!d->is_opening) {
			continue;
		}
		float t = (now_s - d->open_start_s) / DOOR_OPEN_DURATION_S;
		if (t < 0.0f) {
			t = 0.0f;
		}
		if (t > 1.0f) {
			t = 1.0f;
		}
		// Smooth for nicer feel.
		float tt = t * t * (3.0f - 2.0f * t);
		door_apply_wall_state(world, d->wall_index, true, tt, d->closed_tex);
		if (t >= 1.0f - 1e-4f) {
			// Finalize: make portal open and restore base tex.
			int wall_index = d->wall_index;
			if (wall_index >= 0 && wall_index < world->wall_count) {
				int twin = find_twin_portal_wall_index(world, wall_index);
				Wall* w = &world->walls[wall_index];
				Wall* wt = (twin >= 0) ? &world->walls[twin] : NULL;
				w->door_blocked = false;
				w->door_open_t = 1.0f;
				strncpy(w->tex, w->base_tex, sizeof(w->tex) - 1u);
				w->tex[sizeof(w->tex) - 1u] = '\0';
				if (wt) {
					wt->door_blocked = false;
					wt->door_open_t = 1.0f;
					strncpy(wt->tex, wt->base_tex, sizeof(wt->tex) - 1u);
					wt->tex[sizeof(wt->tex) - 1u] = '\0';
				}
			}
			d->is_opening = false;
			d->is_open = true;
			// Long cooldown isn't meaningful once open-only, but keep behavior consistent.
			d->next_allowed_s = now_s + 15.0f;
		}
	}
}

bool doors_try_open_near_player(
	Doors* self,
	World* world,
	const Player* player,
	Notifications* notifications,
	SoundEmitters* sfx,
	float listener_x,
	float listener_y,
	float now_s
) {
	if (!self || !world || !player || !world->walls || world->wall_count <= 0) {
		return false;
	}
	const float interaction_radius = 1.0f;
	float best_dist2 = 1e30f;
	int best = -1;

	for (int i = 0; i < self->door_count; i++) {
		Door* d = &self->doors[i];
		if (d->is_open) {
			continue;
		}
		if (d->wall_index < 0 || d->wall_index >= world->wall_count) {
			continue;
		}
		const Wall* w = &world->walls[d->wall_index];
		// Only interact when player is in one of the adjacent sectors.
		if (player->body.sector != w->front_sector && player->body.sector != w->back_sector) {
			continue;
		}
		float dist2 = player_dist2_to_wall_segment(world, w, player->body.x, player->body.y);
		if (dist2 > interaction_radius * interaction_radius) {
			continue;
		}
		if (dist2 < best_dist2) {
			best_dist2 = dist2;
			best = i;
		}
	}

	if (best < 0) {
		return false;
	}

	DoorsOpenResult r = door_try_open_index(self, world, player, notifications, sfx, listener_x, listener_y, now_s, best);
	return r == DOORS_OPENED;
}

DoorsOpenResult doors_try_open_by_id(
	Doors* self,
	World* world,
	const Player* player,
	Notifications* notifications,
	SoundEmitters* sfx,
	float listener_x,
	float listener_y,
	float now_s,
	const char* door_id
) {
	if (!self || !world || !player || !door_id || door_id[0] == '\0') {
		return DOORS_INVALID;
	}
	for (int i = 0; i < self->door_count; i++) {
		if (strcmp(self->doors[i].id, door_id) == 0) {
			return door_try_open_index(self, world, player, notifications, sfx, listener_x, listener_y, now_s, i);
		}
	}
	return DOORS_NOT_FOUND;
}
