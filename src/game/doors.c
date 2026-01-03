#include "game/doors.h"

#include "game/inventory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
		safe_copy(out->closed_tex, sizeof(out->closed_tex), d->tex);
		safe_copy(out->sound_open, sizeof(out->sound_open), d->sound_open);
		safe_copy(out->required_item, sizeof(out->required_item), d->required_item);
		safe_copy(out->required_item_missing_message, sizeof(out->required_item_missing_message), d->required_item_missing_message);
		out->next_allowed_s = 0.0f;
		out->next_deny_toast_s = 0.0f;

		// Apply initial closed state to the bound wall.
		if (out->wall_index >= 0 && out->wall_index < world->wall_count) {
			int twin = find_twin_portal_wall_index(world, out->wall_index);
			Wall* w = &world->walls[out->wall_index];
			Wall* wt = (twin >= 0) ? &world->walls[twin] : NULL;
			if (!out->is_open) {
				w->door_blocked = true;
				if (wt) {
					wt->door_blocked = true;
				}
				if (out->closed_tex[0] != '\0') {
					strncpy(w->tex, out->closed_tex, sizeof(w->tex) - 1u);
					w->tex[sizeof(w->tex) - 1u] = '\0';
					if (wt) {
						strncpy(wt->tex, out->closed_tex, sizeof(wt->tex) - 1u);
						wt->tex[sizeof(wt->tex) - 1u] = '\0';
					}
				}
			} else {
				w->door_blocked = false;
				if (wt) {
					wt->door_blocked = false;
				}
				// Keep authored base tex (already in w->tex).
			}
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

	Wall* w = &world->walls[d->wall_index];
	int twin = find_twin_portal_wall_index(world, d->wall_index);
	Wall* wt = (twin >= 0) ? &world->walls[twin] : NULL;
	w->door_blocked = false;
	if (wt) {
		wt->door_blocked = false;
	}
	strncpy(w->tex, w->base_tex, sizeof(w->tex) - 1u);
	w->tex[sizeof(w->tex) - 1u] = '\0';
	if (wt) {
		strncpy(wt->tex, wt->base_tex, sizeof(wt->tex) - 1u);
		wt->tex[sizeof(wt->tex) - 1u] = '\0';
	}

	d->is_open = true;
	d->next_allowed_s = now_s + 15.0f;

	if (sfx && d->sound_open[0] != '\0') {
		// Play from door midpoint.
		float wx = 0.0f;
		float wy = 0.0f;
		if (wall_has_valid_vertices(world, w)) {
			Vertex a = world->vertices[w->v0];
			Vertex b = world->vertices[w->v1];
			wx = (a.x + b.x) * 0.5f;
			wy = (a.y + b.y) * 0.5f;
			sound_emitters_play_one_shot_at(sfx, d->sound_open, wx, wy, true, 1.0f, listener_x, listener_y);
		}
	}

	return DOORS_OPENED;
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
