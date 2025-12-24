#include "game/entities.h"

#include "assets/json.h"
#include "core/log.h"

#include "game/collision.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float fmaxf2(float a, float b) {
	return a > b ? a : b;
}

static void physics_body_move_delta_block_portals(PhysicsBody* b, const World* world, float dx, float dy, const PhysicsBodyParams* params) {
	if (!b) {
		return;
	}
	float saved_step = b->step_height;
	// Any portal step delta will be > this, so portals become solid for this move.
	b->step_height = -1.0e9f;
	physics_body_move_delta(b, world, dx, dy, params);
	b->step_height = saved_step;
}

static void physics_body_update_block_portals(PhysicsBody* b, const World* world, float wish_vx, float wish_vy, double dt_s, const PhysicsBodyParams* params) {
	if (!b) {
		return;
	}
	float saved_step = b->step_height;
	// Any portal step delta will be > this, so portals become solid for this update.
	b->step_height = -1.0e9f;
	physics_body_update(b, world, wish_vx, wish_vy, dt_s, params);
	b->step_height = saved_step;
}

static float deg_to_rad2(float deg);

static bool json_get_float2(const JsonDoc* doc, int tok, float* out);
static bool json_get_int2(const JsonDoc* doc, int tok, int* out);

static void spatial_invalidate(EntitySystem* es);
static void spatial_rebuild(EntitySystem* es);
static uint32_t spatial_query_circle_indices(EntitySystem* es, float x, float y, float radius, uint32_t* out_idx, uint32_t out_cap);

static float clampf3(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static bool projectile_autoaim_apply(EntitySystem* es, Entity* proj, const PhysicsBody* player_body, bool target_player) {
	if (!es || !proj || !es->defs) {
		return false;
	}
	const EntityDef* def = &es->defs->defs[proj->def_id];
	if (def->kind != ENTITY_KIND_PROJECTILE) {
		return false;
	}
	if (!es->world) {
		return false;
	}
	float speed = def->u.projectile.speed;
	if (speed <= 1e-4f) {
		return false;
	}

	float ang = deg_to_rad2(proj->yaw_deg);
	float fx = cosf(ang);
	float fy = sinf(ang);
	float rx = -fy;
	float ry = fx;

	// Choose the closest valid target along the projectile's yaw direction.
	float best_forward = 1e30f;
	float best_tx = 0.0f;
	float best_ty = 0.0f;
	float best_tz_center = 0.0f;
	float best_radius = 0.0f;
	bool found = false;

	if (target_player) {
		if (!player_body) {
			return false;
		}
		float vx = player_body->x - proj->body.x;
		float vy = player_body->y - proj->body.y;
		float forward = vx * fx + vy * fy;
		if (forward > 0.05f) {
			float lateral = fabsf(vx * rx + vy * ry);
			float rr = proj->body.radius + player_body->radius;
			if (lateral <= rr) {
				if (collision_line_of_sight(es->world, proj->body.x, proj->body.y, player_body->x, player_body->y)) {
					best_forward = forward;
					best_tx = player_body->x;
					best_ty = player_body->y;
					best_tz_center = player_body->z + 0.5f * player_body->height;
					best_radius = player_body->radius;
					found = true;
				}
			}
		}
	} else {
		for (uint32_t i = 0; i < es->capacity; i++) {
			if (!es->alive[i]) {
				continue;
			}
			Entity* t = &es->entities[i];
			if (t == proj || t->pending_despawn) {
				continue;
			}
			if (!entity_id_is_none(proj->owner) && t->id.index == proj->owner.index && t->id.gen == proj->owner.gen) {
				continue;
			}
			const EntityDef* tdef = &es->defs->defs[t->def_id];
			if (tdef->max_hp <= 0) {
				continue;
			}
			if (tdef->kind == ENTITY_KIND_PICKUP || tdef->kind == ENTITY_KIND_PROJECTILE) {
				continue;
			}

			float vx = t->body.x - proj->body.x;
			float vy = t->body.y - proj->body.y;
			float forward = vx * fx + vy * fy;
			if (forward <= 0.05f || forward >= best_forward) {
				continue;
			}
			float lateral = fabsf(vx * rx + vy * ry);
			float rr = proj->body.radius + t->body.radius;
			if (lateral > rr) {
				continue;
			}
			if (!collision_line_of_sight(es->world, proj->body.x, proj->body.y, t->body.x, t->body.y)) {
				continue;
			}
			best_forward = forward;
			best_tx = t->body.x;
			best_ty = t->body.y;
			best_tz_center = t->body.z + 0.5f * t->body.height;
			best_radius = t->body.radius;
			found = true;
		}
	}

	if (!found) {
		return false;
	}

	// Convert forward distance into a time-of-flight estimate.
	float t_hit = best_forward / speed;
	// Avoid extreme vertical velocities for point-blank shots.
	t_hit = clampf3(t_hit, 0.05f, 10.0f);

	float proj_z_center0 = proj->body.z + 0.5f * proj->body.height;
	float desired_vz = (best_tz_center - proj_z_center0) / t_hit;

	// Mild clamp to avoid absurd values; still allows significant height differences.
	float max_vz = speed * 4.0f;
	proj->body.vz = clampf3(desired_vz, -max_vz, max_vz);
	(void)best_tx;
	(void)best_ty;
	(void)best_radius;
	return true;
}

bool entity_system_projectile_autoaim(EntitySystem* es, EntityId projectile_id, bool target_player, const PhysicsBody* player_body) {
	if (!es) {
		return false;
	}
	Entity* proj = NULL;
	if (!entity_system_resolve(es, projectile_id, &proj)) {
		return false;
	}
	return projectile_autoaim_apply(es, proj, player_body, target_player);
}

static void* xrealloc(void* p, size_t n) {
	void* q = realloc(p, n);
	return q;
}

void entity_defs_init(EntityDefs* defs) {
	if (!defs) {
		return;
	}
	memset(defs, 0, sizeof(*defs));
}

void entity_defs_destroy(EntityDefs* defs) {
	if (!defs) {
		return;
	}
	free(defs->defs);
	memset(defs, 0, sizeof(*defs));
}

static bool parse_kind(const JsonDoc* doc, int tok, EntityKind* out) {
	if (!doc || tok < 0 || !out) {
		return false;
	}
	if (!json_token_is_string(doc, tok)) {
		return false;
	}
	StringView sv;
	if (!json_get_string(doc, tok, &sv)) {
		return false;
	}
	if (sv.len == 6 && strncmp(sv.data, "pickup", 6) == 0) {
		*out = ENTITY_KIND_PICKUP;
		return true;
	}
	if (sv.len == 10 && strncmp(sv.data, "projectile", 10) == 0) {
		*out = ENTITY_KIND_PROJECTILE;
		return true;
	}
	if (sv.len == 6 && strncmp(sv.data, "turret", 6) == 0) {
		*out = ENTITY_KIND_TURRET;
		return true;
	}
	if (sv.len == 5 && strncmp(sv.data, "enemy", 5) == 0) {
		*out = ENTITY_KIND_ENEMY;
		return true;
	}
	if (sv.len == 7 && strncmp(sv.data, "support", 7) == 0) {
		*out = ENTITY_KIND_SUPPORT;
		return true;
	}
	return false;
}

static bool json_get_int_required(const JsonDoc* doc, int obj_tok, const char* key, int* out) {
	if (!doc || obj_tok < 0 || !key || !out) {
		return false;
	}
	int t = -1;
	if (!json_object_get(doc, obj_tok, key, &t) || t < 0) {
		return false;
	}
	return json_get_int2(doc, t, out);
}

static bool parse_enemy_anim(const JsonDoc* doc, int t_anim, const char* def_name, const char* anim_name, EntityDefEnemyAnim* out) {
	if (!doc || t_anim < 0 || !out) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	out->start = 0;
	out->count = 1;
	out->fps = 6.0f;
	if (!json_token_is_object(doc, t_anim)) {
		log_error("entity def '%s' enemy.animations.%s must be object", def_name, anim_name);
		return false;
	}
	int start = 0;
	int count = 1;
	float fps = 6.0f;
	if (!json_get_int_required(doc, t_anim, "start", &start) || start < 0) {
		log_error("entity def '%s' enemy.animations.%s.start invalid", def_name, anim_name);
		return false;
	}
	if (!json_get_int_required(doc, t_anim, "count", &count) || count <= 0) {
		log_error("entity def '%s' enemy.animations.%s.count invalid", def_name, anim_name);
		return false;
	}
	int t_fps = -1;
	if (json_object_get(doc, t_anim, "fps", &t_fps) && t_fps >= 0) {
		(void)json_get_float2(doc, t_fps, &fps);
	}
	if (fps <= 0.0f) {
		fps = 6.0f;
	}
	out->start = start;
	out->count = count;
	out->fps = fps;
	return true;
}

static bool parse_ammo_type_sv(StringView sv, AmmoType* out) {
	if (!out) {
		return false;
	}
	if (sv.len == 7 && strncmp(sv.data, "bullets", 7) == 0) {
		*out = AMMO_BULLETS;
		return true;
	}
	if (sv.len == 6 && strncmp(sv.data, "shells", 6) == 0) {
		*out = AMMO_SHELLS;
		return true;
	}
	if (sv.len == 5 && strncmp(sv.data, "cells", 5) == 0) {
		*out = AMMO_CELLS;
		return true;
	}
	return false;
}

static bool json_get_float2(const JsonDoc* doc, int tok, float* out) {
	double d = 0.0;
	if (!json_get_double(doc, tok, &d)) {
		return false;
	}
	*out = (float)d;
	return true;
}

static bool json_get_int2(const JsonDoc* doc, int tok, int* out) {
	double d = 0.0;
	if (!json_get_double(doc, tok, &d)) {
		return false;
	}
	*out = (int)d;
	return true;
}

static bool defs_push(EntityDefs* defs, const EntityDef* def) {
	if (!defs || !def) {
		return false;
	}
	if (defs->count == defs->capacity) {
		uint32_t new_cap = defs->capacity ? defs->capacity * 2u : 16u;
		EntityDef* nd = (EntityDef*)xrealloc(defs->defs, (size_t)new_cap * sizeof(EntityDef));
		if (!nd) {
			return false;
		}
		defs->defs = nd;
		defs->capacity = new_cap;
	}
	defs->defs[defs->count++] = *def;
	return true;
}

uint32_t entity_defs_find(const EntityDefs* defs, const char* name) {
	if (!defs || !name || name[0] == '\0') {
		return UINT32_MAX;
	}
	for (uint32_t i = 0; i < defs->count; i++) {
		if (strcmp(defs->defs[i].name, name) == 0) {
			return i;
		}
	}
	return UINT32_MAX;
}

bool entity_defs_load(EntityDefs* defs, const AssetPaths* paths) {
	if (!defs || !paths) {
		return false;
	}
	entity_defs_destroy(defs);
	entity_defs_init(defs);

	char* full = asset_path_join(paths, "Entities", "entities.json");
	if (!full) {
		log_error("entity defs: could not allocate path");
		return false;
	}
	JsonDoc doc;
	if (!json_doc_load_file(&doc, full)) {
		log_warn("entity defs: missing or unreadable: %s", full);
		free(full);
		return false;
	}
	free(full);

	if (doc.token_count < 1 || !json_token_is_object(&doc, 0)) {
		log_error("entity defs root must be an object");
		json_doc_destroy(&doc);
		return false;
	}

	int t_defs = -1;
	if (!json_object_get(&doc, 0, "defs", &t_defs) || t_defs < 0 || !json_token_is_array(&doc, t_defs)) {
		log_error("entity defs missing required array field 'defs'");
		json_doc_destroy(&doc);
		return false;
	}

	int n = json_array_size(&doc, t_defs);
	for (int i = 0; i < n; i++) {
		int t_def = json_array_nth(&doc, t_defs, i);
		if (!json_token_is_object(&doc, t_def)) {
			log_error("entity defs[%d] must be an object", i);
			json_doc_destroy(&doc);
			entity_defs_destroy(defs);
			return false;
		}

		EntityDef def;
		memset(&def, 0, sizeof(def));
		def.kind = ENTITY_KIND_INVALID;
		def.radius = 0.35f;
		def.height = 1.0f;
		def.max_hp = 0;

		int t_name = -1;
		int t_kind = -1;
		(void)json_object_get(&doc, t_def, "name", &t_name);
		(void)json_object_get(&doc, t_def, "kind", &t_kind);
		if (t_name < 0 || t_kind < 0) {
			log_error("entity defs[%d] missing name/kind", i);
			json_doc_destroy(&doc);
			entity_defs_destroy(defs);
			return false;
		}
		StringView sv_name;
		if (!json_get_string(&doc, t_name, &sv_name) || sv_name.len == 0 || sv_name.len >= sizeof(def.name)) {
			log_error("entity defs[%d] invalid name", i);
			json_doc_destroy(&doc);
			entity_defs_destroy(defs);
			return false;
		}
		snprintf(def.name, sizeof(def.name), "%.*s", (int)sv_name.len, sv_name.data);
		if (!parse_kind(&doc, t_kind, &def.kind)) {
			log_error("entity defs[%d] invalid kind", i);
			json_doc_destroy(&doc);
			entity_defs_destroy(defs);
			return false;
		}

		// Optional bounds overrides
		int t_radius = -1, t_height = -1;
		if (json_object_get(&doc, t_def, "radius", &t_radius) && t_radius >= 0) {
			(void)json_get_float2(&doc, t_radius, &def.radius);
		}
		if (json_object_get(&doc, t_def, "height", &t_height) && t_height >= 0) {
			(void)json_get_float2(&doc, t_height, &def.height);
		}

		// Optional common metadata
		int t_max_hp = -1;
		if (json_object_get(&doc, t_def, "max_hp", &t_max_hp) && t_max_hp >= 0) {
			int mhp = 0;
			if (!json_get_int2(&doc, t_max_hp, &mhp) || mhp < 0) {
				log_error("entity def '%s' max_hp invalid", def.name);
				json_doc_destroy(&doc);
				entity_defs_destroy(defs);
				return false;
			}
			def.max_hp = mhp;
		}

		// Sprite (preferred: object with file/frames/scale/z_offset; legacy: string filename)
		memset(&def.sprite, 0, sizeof(def.sprite));
		def.sprite.scale = 1.0f;
		def.sprite.z_offset = 0.0f;
		def.sprite.frames.count = 1;
		int t_sprite = -1;
		if (json_object_get(&doc, t_def, "sprite", &t_sprite) && t_sprite >= 0) {
			if (json_token_is_string(&doc, t_sprite)) {
				// Legacy: sprite: "file.png"
				StringView sv;
				if (json_get_string(&doc, t_sprite, &sv) && sv.len > 0 && sv.len < (int)sizeof(def.sprite.file.name)) {
					snprintf(def.sprite.file.name, sizeof(def.sprite.file.name), "%.*s", (int)sv.len, sv.data);
				}
			} else if (json_token_is_object(&doc, t_sprite)) {
				int t_file = -1;
				int t_frames = -1;
				if (!json_object_get(&doc, t_sprite, "file", &t_file) || t_file < 0 || !json_token_is_object(&doc, t_file)) {
					log_error("entity def '%s' sprite missing object 'file'", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				if (!json_object_get(&doc, t_sprite, "frames", &t_frames) || t_frames < 0 || !json_token_is_object(&doc, t_frames)) {
					log_error("entity def '%s' sprite missing object 'frames'", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}

				// file.name
				int t_name = -1;
				if (!json_object_get(&doc, t_file, "name", &t_name) || t_name < 0 || !json_token_is_string(&doc, t_name)) {
					log_error("entity def '%s' sprite.file missing string 'name'", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				StringView sv_name;
				if (!json_get_string(&doc, t_name, &sv_name) || sv_name.len <= 0 || sv_name.len >= (int)sizeof(def.sprite.file.name)) {
					log_error("entity def '%s' sprite.file.name invalid", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				snprintf(def.sprite.file.name, sizeof(def.sprite.file.name), "%.*s", (int)sv_name.len, sv_name.data);

				// file.dimensions.x/y
				int t_fdim = -1;
				if (!json_object_get(&doc, t_file, "dimensions", &t_fdim) || t_fdim < 0 || !json_token_is_object(&doc, t_fdim)) {
					log_error("entity def '%s' sprite.file missing object 'dimensions'", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				int t_fx = -1, t_fy = -1;
				if (!json_object_get(&doc, t_fdim, "x", &t_fx) || !json_object_get(&doc, t_fdim, "y", &t_fy)) {
					log_error("entity def '%s' sprite.file.dimensions missing x/y", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				(void)json_get_int2(&doc, t_fx, &def.sprite.file.width);
				(void)json_get_int2(&doc, t_fy, &def.sprite.file.height);
				if (def.sprite.file.width <= 0 || def.sprite.file.height <= 0) {
					log_error("entity def '%s' sprite.file.dimensions must be positive", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}

				// frames.count
				int t_count = -1;
				if (!json_object_get(&doc, t_frames, "count", &t_count)) {
					log_error("entity def '%s' sprite.frames missing count", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				(void)json_get_int2(&doc, t_count, &def.sprite.frames.count);
				if (def.sprite.frames.count <= 0) {
					log_error("entity def '%s' sprite.frames.count must be >= 1", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}

				// frames.dimensions.x/y
				int t_frdim = -1;
				if (!json_object_get(&doc, t_frames, "dimensions", &t_frdim) || t_frdim < 0 || !json_token_is_object(&doc, t_frdim)) {
					log_error("entity def '%s' sprite.frames missing object 'dimensions'", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				int t_frx = -1, t_fry = -1;
				if (!json_object_get(&doc, t_frdim, "x", &t_frx) || !json_object_get(&doc, t_frdim, "y", &t_fry)) {
					log_error("entity def '%s' sprite.frames.dimensions missing x/y", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				(void)json_get_int2(&doc, t_frx, &def.sprite.frames.width);
				(void)json_get_int2(&doc, t_fry, &def.sprite.frames.height);
				if (def.sprite.frames.width <= 0 || def.sprite.frames.height <= 0) {
					log_error("entity def '%s' sprite.frames.dimensions must be positive", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}

				// scale/z_offset
				int t_scale = -1;
				if (json_object_get(&doc, t_sprite, "scale", &t_scale) && t_scale >= 0) {
					(void)json_get_float2(&doc, t_scale, &def.sprite.scale);
				}
				int t_zoff = -1;
				if (json_object_get(&doc, t_sprite, "z_offset", &t_zoff) && t_zoff >= 0) {
					(void)json_get_float2(&doc, t_zoff, &def.sprite.z_offset);
				}
				if (def.sprite.scale <= 0.0f) {
					def.sprite.scale = 1.0f;
				}

				// Validate frame packing for a horizontal strip.
				if (def.sprite.frames.width * def.sprite.frames.count > def.sprite.file.width || def.sprite.frames.height > def.sprite.file.height) {
					log_error("entity def '%s' sprite frames do not fit in file (horizontal strip)", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
			}
		}
		def.radius = fmaxf2(def.radius, 0.01f);
		def.height = fmaxf2(def.height, 0.01f);

		// Kind-specific payloads
		if (def.kind == ENTITY_KIND_PICKUP) {
			int t_pickup = -1;
			if (!json_object_get(&doc, t_def, "pickup", &t_pickup) || t_pickup < 0 || !json_token_is_object(&doc, t_pickup)) {
				log_error("entity def '%s' kind pickup missing object field 'pickup'", def.name);
				json_doc_destroy(&doc);
				entity_defs_destroy(defs);
				return false;
			}
			int t_heal = -1, t_tr = -1;
			int t_ammo_type = -1, t_ammo_amount = -1;
			(void)json_object_get(&doc, t_pickup, "heal_amount", &t_heal);
			(void)json_object_get(&doc, t_pickup, "ammo_type", &t_ammo_type);
			(void)json_object_get(&doc, t_pickup, "ammo_amount", &t_ammo_amount);

			def.u.pickup.type = PICKUP_TYPE_HEALTH;
			def.u.pickup.heal_amount = 0;
			def.u.pickup.ammo_type = AMMO_BULLETS;
			def.u.pickup.ammo_amount = 0;

			bool has_heal = (t_heal >= 0);
			bool has_ammo = (t_ammo_type >= 0 && t_ammo_amount >= 0);
			if (!has_heal && !has_ammo) {
				log_error("entity def '%s' pickup must specify heal_amount or (ammo_type + ammo_amount)", def.name);
				json_doc_destroy(&doc);
				entity_defs_destroy(defs);
				return false;
			}

			if (has_heal) {
				double heal_d = 0.0;
				if (!json_get_double(&doc, t_heal, &heal_d)) {
					log_error("entity def '%s' pickup heal_amount invalid", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				def.u.pickup.type = PICKUP_TYPE_HEALTH;
				def.u.pickup.heal_amount = (int)heal_d;
			} else {
				if (!json_token_is_string(&doc, t_ammo_type)) {
					log_error("entity def '%s' pickup ammo_type must be string", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				StringView sv_at;
				if (!json_get_string(&doc, t_ammo_type, &sv_at) || sv_at.len == 0) {
					log_error("entity def '%s' pickup ammo_type invalid", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				AmmoType at;
				if (!parse_ammo_type_sv(sv_at, &at)) {
					log_error("entity def '%s' pickup ammo_type must be bullets/shells/cells", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				double amt_d = 0.0;
				if (!json_get_double(&doc, t_ammo_amount, &amt_d) || amt_d <= 0.0) {
					log_error("entity def '%s' pickup ammo_amount invalid", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				def.u.pickup.type = PICKUP_TYPE_AMMO;
				def.u.pickup.ammo_type = at;
				def.u.pickup.ammo_amount = (int)amt_d;
			}
			// trigger_radius defaults to def.radius if absent
			def.u.pickup.trigger_radius = def.radius;
			if (json_object_get(&doc, t_pickup, "trigger_radius", &t_tr) && t_tr >= 0) {
				(void)json_get_float2(&doc, t_tr, &def.u.pickup.trigger_radius);
			}
			def.u.pickup.trigger_radius = fmaxf2(def.u.pickup.trigger_radius, 0.01f);

			def.u.pickup.pickup_sound[0] = '\0';
			def.u.pickup.pickup_sound_gain = 1.0f;
			int t_sound = -1, t_gain = -1;
			if (json_object_get(&doc, t_pickup, "pickup_sound", &t_sound) && t_sound >= 0 && json_token_is_string(&doc, t_sound)) {
				StringView sv;
				if (json_get_string(&doc, t_sound, &sv) && sv.len < sizeof(def.u.pickup.pickup_sound)) {
					snprintf(def.u.pickup.pickup_sound, sizeof(def.u.pickup.pickup_sound), "%.*s", (int)sv.len, sv.data);
				}
			}
			if (json_object_get(&doc, t_pickup, "pickup_sound_gain", &t_gain) && t_gain >= 0) {
				(void)json_get_float2(&doc, t_gain, &def.u.pickup.pickup_sound_gain);
			}
		}

		if (def.kind == ENTITY_KIND_PROJECTILE) {
			int t_proj = -1;
			if (!json_object_get(&doc, t_def, "projectile", &t_proj) || t_proj < 0 || !json_token_is_object(&doc, t_proj)) {
				log_error("entity def '%s' kind projectile missing object field 'projectile'", def.name);
				json_doc_destroy(&doc);
				entity_defs_destroy(defs);
				return false;
			}
			def.u.projectile.speed = 8.0f;
			def.u.projectile.lifetime_s = 1.0f;
			def.u.projectile.damage = 10;
			def.u.projectile.impact_sound[0] = '\0';
			def.u.projectile.impact_sound_gain = 1.0f;

			int t_speed = -1, t_life = -1, t_damage = -1, t_sound = -1, t_gain = -1;
			if (json_object_get(&doc, t_proj, "speed", &t_speed) && t_speed >= 0) {
				(void)json_get_float2(&doc, t_speed, &def.u.projectile.speed);
			}
			if (json_object_get(&doc, t_proj, "lifetime_s", &t_life) && t_life >= 0) {
				(void)json_get_float2(&doc, t_life, &def.u.projectile.lifetime_s);
			}
			if (json_object_get(&doc, t_proj, "damage", &t_damage) && t_damage >= 0) {
				int dmg = 0;
				if (!json_get_int2(&doc, t_damage, &dmg) || dmg < 0) {
					log_error("entity def '%s' projectile.damage invalid", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				def.u.projectile.damage = dmg;
			}
			if (json_object_get(&doc, t_proj, "impact_sound", &t_sound) && t_sound >= 0 && json_token_is_string(&doc, t_sound)) {
				StringView sv;
				if (json_get_string(&doc, t_sound, &sv) && sv.len < sizeof(def.u.projectile.impact_sound)) {
					snprintf(def.u.projectile.impact_sound, sizeof(def.u.projectile.impact_sound), "%.*s", (int)sv.len, sv.data);
				}
			}
			if (json_object_get(&doc, t_proj, "impact_sound_gain", &t_gain) && t_gain >= 0) {
				(void)json_get_float2(&doc, t_gain, &def.u.projectile.impact_sound_gain);
			}
			def.u.projectile.speed = fmaxf2(def.u.projectile.speed, 0.0f);
			def.u.projectile.lifetime_s = fmaxf2(def.u.projectile.lifetime_s, 0.0f);
		}

		if (def.kind == ENTITY_KIND_ENEMY) {
			int t_enemy = -1;
			if (!json_object_get(&doc, t_def, "enemy", &t_enemy) || t_enemy < 0 || !json_token_is_object(&doc, t_enemy)) {
				log_error("entity def '%s' kind enemy missing object field 'enemy'", def.name);
				json_doc_destroy(&doc);
				entity_defs_destroy(defs);
				return false;
			}
			EntityDefEnemy* ed = &def.u.enemy;
			memset(ed, 0, sizeof(*ed));
			ed->move_speed = 1.2f;
			ed->engage_range = 6.0f;
			ed->disengage_range = 10.0f;
			ed->attack_range = 0.9f;
			ed->attack_windup_s = 0.25f;
			ed->attack_cooldown_s = 0.9f;
			ed->attack_damage = 10;
			ed->damaged_time_s = 0.25f;
			ed->dying_time_s = 0.7f;
			ed->dead_time_s = 0.8f;

			int t_move = -1, t_eng = -1, t_dis = -1, t_ar = -1;
			int t_wind = -1, t_cd = -1, t_dmg = -1;
			int t_damaged_t = -1, t_dying_t = -1, t_dead_t = -1;
			(void)json_object_get(&doc, t_enemy, "move_speed", &t_move);
			(void)json_object_get(&doc, t_enemy, "engage_range", &t_eng);
			(void)json_object_get(&doc, t_enemy, "disengage_range", &t_dis);
			(void)json_object_get(&doc, t_enemy, "attack_range", &t_ar);
			(void)json_object_get(&doc, t_enemy, "attack_windup_s", &t_wind);
			(void)json_object_get(&doc, t_enemy, "attack_cooldown_s", &t_cd);
			(void)json_object_get(&doc, t_enemy, "attack_damage", &t_dmg);
			(void)json_object_get(&doc, t_enemy, "damaged_time_s", &t_damaged_t);
			(void)json_object_get(&doc, t_enemy, "dying_time_s", &t_dying_t);
			(void)json_object_get(&doc, t_enemy, "dead_time_s", &t_dead_t);
			if (t_move >= 0) {
				(void)json_get_float2(&doc, t_move, &ed->move_speed);
			}
			if (t_eng >= 0) {
				(void)json_get_float2(&doc, t_eng, &ed->engage_range);
			}
			if (t_dis >= 0) {
				(void)json_get_float2(&doc, t_dis, &ed->disengage_range);
			}
			if (t_ar >= 0) {
				(void)json_get_float2(&doc, t_ar, &ed->attack_range);
			}
			if (t_wind >= 0) {
				(void)json_get_float2(&doc, t_wind, &ed->attack_windup_s);
			}
			if (t_cd >= 0) {
				(void)json_get_float2(&doc, t_cd, &ed->attack_cooldown_s);
			}
			if (t_dmg >= 0) {
				int dmg = 0;
				if (!json_get_int2(&doc, t_dmg, &dmg) || dmg < 0) {
					log_error("entity def '%s' enemy.attack_damage invalid", def.name);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				ed->attack_damage = dmg;
			}
			if (t_damaged_t >= 0) {
				(void)json_get_float2(&doc, t_damaged_t, &ed->damaged_time_s);
			}
			if (t_dying_t >= 0) {
				(void)json_get_float2(&doc, t_dying_t, &ed->dying_time_s);
			}
			if (t_dead_t >= 0) {
				(void)json_get_float2(&doc, t_dead_t, &ed->dead_time_s);
			}
			ed->move_speed = fmaxf2(ed->move_speed, 0.0f);
			ed->engage_range = fmaxf2(ed->engage_range, 0.0f);
			ed->disengage_range = fmaxf2(ed->disengage_range, ed->engage_range);
			ed->attack_range = fmaxf2(ed->attack_range, 0.0f);
			ed->attack_windup_s = fmaxf2(ed->attack_windup_s, 0.0f);
			ed->attack_cooldown_s = fmaxf2(ed->attack_cooldown_s, ed->attack_windup_s + 0.01f);
			ed->damaged_time_s = fmaxf2(ed->damaged_time_s, 0.0f);
			ed->dying_time_s = fmaxf2(ed->dying_time_s, 0.0f);
			ed->dead_time_s = fmaxf2(ed->dead_time_s, 0.0f);

			// Animations
			int t_anims = -1;
			if (!json_object_get(&doc, t_enemy, "animations", &t_anims) || t_anims < 0 || !json_token_is_object(&doc, t_anims)) {
				log_error("entity def '%s' enemy missing object 'animations'", def.name);
				json_doc_destroy(&doc);
				entity_defs_destroy(defs);
				return false;
			}
			const struct { const char* key; EntityDefEnemyAnim* out; } anim_keys[] = {
				{"idle", &ed->anim_idle},
				{"engaged", &ed->anim_engaged},
				{"attack", &ed->anim_attack},
				{"damaged", &ed->anim_damaged},
				{"dying", &ed->anim_dying},
				{"dead", &ed->anim_dead},
			};
			for (int ai = 0; ai < (int)(sizeof(anim_keys) / sizeof(anim_keys[0])); ai++) {
				int t_a = -1;
				if (!json_object_get(&doc, t_anims, anim_keys[ai].key, &t_a) || t_a < 0) {
					log_error("entity def '%s' enemy.animations missing '%s'", def.name, anim_keys[ai].key);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
				if (!parse_enemy_anim(&doc, t_a, def.name, anim_keys[ai].key, anim_keys[ai].out)) {
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
			}

			// Validate animation frame ranges against sprite.frames.count
			int sc = def.sprite.frames.count > 0 ? def.sprite.frames.count : 1;
			const EntityDefEnemyAnim* anims[] = {&ed->anim_idle, &ed->anim_engaged, &ed->anim_attack, &ed->anim_damaged, &ed->anim_dying, &ed->anim_dead};
			for (int k = 0; k < (int)(sizeof(anims) / sizeof(anims[0])); k++) {
				int start = anims[k]->start;
				int count = anims[k]->count;
				if (start < 0 || count <= 0 || start + count > sc) {
					log_error("entity def '%s' enemy animation out of range (start=%d count=%d sprite_frames=%d)", def.name, start, count, sc);
					json_doc_destroy(&doc);
					entity_defs_destroy(defs);
					return false;
				}
			}

			// Enemies should be damageable.
			if (def.max_hp <= 0) {
				log_error("entity def '%s' kind enemy requires max_hp > 0", def.name);
				json_doc_destroy(&doc);
				entity_defs_destroy(defs);
				return false;
			}
		}

		// Require unique names.
		if (entity_defs_find(defs, def.name) != UINT32_MAX) {
			log_error("duplicate entity def name '%s'", def.name);
			json_doc_destroy(&doc);
			entity_defs_destroy(defs);
			return false;
		}

		if (!defs_push(defs, &def)) {
			log_error("out of memory adding entity def '%s'", def.name);
			json_doc_destroy(&doc);
			entity_defs_destroy(defs);
			return false;
		}
	}

	json_doc_destroy(&doc);
	log_info("Loaded %u entity defs", defs->count);
	return true;
}

static void entity_slot_clear(EntitySystem* es, uint32_t idx) {
	Entity* e = &es->entities[idx];
	memset(e, 0, sizeof(*e));
	e->id.index = idx;
	e->id.gen = es->generation[idx];
	e->def_id = 0;
	e->state = ENTITY_STATE_SPAWNING;
	e->target = entity_id_none();
	e->owner = entity_id_none();
	e->sprite_frame = 0u;
	e->attack_has_hit = false;
}

static void entity_events_clear(EntitySystem* es) {
	if (!es) {
		return;
	}
	es->event_count = 0u;
}

static void entity_event_push(EntitySystem* es, EntityEvent ev) {
	if (!es) {
		return;
	}
	if (es->event_count >= es->event_cap) {
		// Drop if full (deterministic, no allocations in tick).
		return;
	}
	es->events[es->event_count++] = ev;
}

void entity_system_init(EntitySystem* es, uint32_t max_entities) {
	if (!es) {
		return;
	}
	memset(es, 0, sizeof(*es));
	es->capacity = max_entities ? max_entities : 256u;
	es->entities = (Entity*)calloc((size_t)es->capacity, sizeof(Entity));
	es->generation = (uint32_t*)calloc((size_t)es->capacity, sizeof(uint32_t));
	es->free_next = (uint32_t*)calloc((size_t)es->capacity, sizeof(uint32_t));
	es->alive = (uint8_t*)calloc((size_t)es->capacity, sizeof(uint8_t));
	es->events = (EntityEvent*)calloc((size_t)es->capacity, sizeof(EntityEvent));
	if (!es->entities || !es->generation || !es->free_next || !es->alive) {
		log_error("entity_system_init: out of memory");
		return;
	}
	if (!es->events) {
		log_error("entity_system_init: out of memory (events)");
		return;
	}

	// Spatial hash
	es->spatial_cell_size = 1.0f;
	es->spatial_bucket_count = 2048u; // power of two
	es->spatial_head = (uint32_t*)malloc((size_t)es->spatial_bucket_count * sizeof(uint32_t));
	es->spatial_next = (uint32_t*)malloc((size_t)es->capacity * sizeof(uint32_t));
	es->spatial_seen = (uint32_t*)calloc((size_t)es->capacity, sizeof(uint32_t));
	es->spatial_stamp = 1u;
	es->spatial_valid = false;
	if (!es->spatial_head || !es->spatial_next || !es->spatial_seen) {
		log_error("entity_system_init: out of memory (spatial)");
		return;
	}
	for (uint32_t bi = 0; bi < es->spatial_bucket_count; bi++) {
		es->spatial_head[bi] = UINT32_MAX;
	}
	for (uint32_t i = 0; i < es->capacity; i++) {
		es->free_next[i] = i + 1u;
		es->generation[i] = 1u;
		entity_slot_clear(es, i);
	}
	es->free_next[es->capacity - 1u] = UINT32_MAX;
	es->free_head = 0u;

	es->despawn_cap = 256u;
	es->despawn_queue = (EntityId*)malloc((size_t)es->despawn_cap * sizeof(EntityId));
	es->despawn_count = 0u;

	es->event_cap = es->capacity;
	es->event_count = 0u;
}

void entity_system_shutdown(EntitySystem* es) {
	if (!es) {
		return;
	}
	free(es->entities);
	free(es->generation);
	free(es->free_next);
	free(es->alive);
	free(es->despawn_queue);
	free(es->events);
	free(es->spatial_head);
	free(es->spatial_next);
	free(es->spatial_seen);
	memset(es, 0, sizeof(*es));
}

void entity_system_reset(EntitySystem* es, const World* world, const EntityDefs* defs) {
	if (!es) {
		return;
	}
	es->world = world;
	es->defs = defs;
	// Clear all alive slots; preserve generation counters.
	for (uint32_t i = 0; i < es->capacity; i++) {
		es->alive[i] = 0;
		es->generation[i] += 1u;
		entity_slot_clear(es, i);
		es->free_next[i] = i + 1u;
	}
	es->free_next[es->capacity - 1u] = UINT32_MAX;
	es->free_head = 0u;
	es->alive_count = 0u;
	es->despawn_count = 0u;
	entity_events_clear(es);
	spatial_invalidate(es);
}

const EntityEvent* entity_system_events(const EntitySystem* es, uint32_t* out_count) {
	if (out_count) {
		*out_count = es ? es->event_count : 0u;
	}
	return es ? es->events : NULL;
}

static bool alloc_slot(EntitySystem* es, uint32_t* out_idx) {
	if (!es || !out_idx) {
		return false;
	}
	uint32_t idx = es->free_head;
	if (idx == UINT32_MAX) {
		return false;
	}
	es->free_head = es->free_next[idx];
	es->free_next[idx] = UINT32_MAX;
	*out_idx = idx;
	return true;
}

static void free_slot(EntitySystem* es, uint32_t idx) {
	if (!es || idx >= es->capacity) {
		return;
	}
	es->alive[idx] = 0;
	es->alive_count--;
	es->generation[idx] += 1u;
	entity_slot_clear(es, idx);
	es->free_next[idx] = es->free_head;
	es->free_head = idx;
}

bool entity_system_spawn(EntitySystem* es, uint32_t def_index, float x, float y, float yaw_deg, int sector, EntityId* out_id) {
	if (!es || !es->defs || def_index >= es->defs->count) {
		return false;
	}
	uint32_t idx = UINT32_MAX;
	if (!alloc_slot(es, &idx)) {
		log_warn("entity spawn failed: out of slots");
		return false;
	}
	es->alive[idx] = 1;
	es->alive_count++;

	Entity* e = &es->entities[idx];
	entity_slot_clear(es, idx);
	e->def_id = (uint16_t)def_index;
	e->state = ENTITY_STATE_IDLE;
	e->state_time = 0.0f;
	e->pending_despawn = false;
	e->hp = es->defs->defs[def_index].max_hp;
	e->yaw_deg = yaw_deg;
	e->sprite_frame = 0u;
	e->owner = entity_id_none();
	e->attack_has_hit = false;

	float z = 0.0f;
	if (es->world && (unsigned)sector < (unsigned)es->world->sector_count) {
		z = es->world->sectors[sector].floor_z;
	}
	const EntityDef* def = &es->defs->defs[def_index];
	float step_h = 0.2f;
	if (def->kind == ENTITY_KIND_ENEMY) {
		step_h = 1.0f;
	}
	physics_body_init(&e->body, x, y, z, def->radius, def->height, step_h);
	e->body.sector = sector;
	e->body.last_valid_sector = sector;
	spatial_invalidate(es);


	if (out_id) {
		*out_id = e->id;
	}
	return true;
}

bool entity_system_emit_event(EntitySystem* es, EntityEvent ev) {
	if (!es) {
		return false;
	}
	if (es->event_count >= es->event_cap) {
		return false;
	}
	es->events[es->event_count++] = ev;
	return true;
}

void entity_system_request_despawn(EntitySystem* es, EntityId id) {
	if (!es || entity_id_is_none(id)) {
		return;
	}
	Entity* e = NULL;
	if (!entity_system_resolve(es, id, &e)) {
		return;
	}
	if (e->pending_despawn) {
		return;
	}
	e->pending_despawn = true;
	if (es->despawn_count == es->despawn_cap) {
		uint32_t new_cap = es->despawn_cap ? es->despawn_cap * 2u : 256u;
		EntityId* nq = (EntityId*)xrealloc(es->despawn_queue, (size_t)new_cap * sizeof(EntityId));
		if (!nq) {
			return;
		}
		es->despawn_queue = nq;
		es->despawn_cap = new_cap;
	}
	es->despawn_queue[es->despawn_count++] = id;
	spatial_invalidate(es);
}

bool entity_system_resolve(EntitySystem* es, EntityId id, Entity** out) {
	if (!es || !out || entity_id_is_none(id)) {
		return false;
	}
	if (id.index >= es->capacity) {
		return false;
	}
	if (!es->alive[id.index]) {
		return false;
	}
	if (es->generation[id.index] != id.gen) {
		return false;
	}
	*out = &es->entities[id.index];
	return true;
}

void entity_system_spawn_map(EntitySystem* es, const MapEntityPlacement* placements, int placement_count) {
	if (!es || !placements || placement_count <= 0) {
		return;
	}
	int spawned = 0;
	int skipped = 0;
	for (int i = 0; i < placement_count; i++) {
		const MapEntityPlacement* p = &placements[i];
		if (p->def_name[0] == '\0' || p->sector < 0) {
			skipped++;
			if (placement_count <= 16) {
				log_warn(
					"skip map entity %d: def='%s' sector=%d pos=(%.2f,%.2f)",
					i,
					p->def_name[0] ? p->def_name : "(empty)",
					p->sector,
					p->x,
					p->y
				);
			}
			continue;
		}
		uint32_t def_idx = entity_defs_find(es->defs, p->def_name);
		if (def_idx == UINT32_MAX) {
			log_warn("map entity '%s' not found in entity defs", p->def_name);
			skipped++;
			continue;
		}
		if (entity_system_spawn(es, def_idx, p->x, p->y, p->yaw_deg, p->sector, NULL)) {
			spawned++;
		} else {
			skipped++;
		}
	}
	log_info("Spawned %d/%d map entities (skipped=%d)", spawned, placement_count, skipped);
}

static void apply_despawns(EntitySystem* es) {
	if (!es || es->despawn_count == 0) {
		return;
	}
	for (uint32_t i = 0; i < es->despawn_count; i++) {
		EntityId id = es->despawn_queue[i];
		Entity* e = NULL;
		if (!entity_system_resolve(es, id, &e)) {
			continue;
		}
		free_slot(es, id.index);
	}
	es->despawn_count = 0;
	spatial_invalidate(es);
}

void entity_system_flush(EntitySystem* es) {
	apply_despawns(es);
}

void entity_system_tick(EntitySystem* es, const PhysicsBody* player_body, float dt_s) {
	if (!es || !es->defs || !player_body) {
		return;
	}
	entity_events_clear(es);

	// Pass 1: update movement/state (no spatial queries).
	for (uint32_t i = 0; i < es->capacity; i++) {
		if (!es->alive[i]) {
			continue;
		}
		Entity* e = &es->entities[i];
		e->state_time += dt_s;
		const EntityDef* def = &es->defs->defs[e->def_id];

		// Deterministic sprite frame selection.
		e->sprite_frame = 0u;
		if (def->kind == ENTITY_KIND_PROJECTILE && def->sprite.frames.count > 1) {
			const float anim_fps = 12.0f;
			uint32_t f = (uint32_t)floorf(e->state_time * anim_fps);
			e->sprite_frame = (uint16_t)(f % (uint32_t)def->sprite.frames.count);
		}
		if (def->kind == ENTITY_KIND_ENEMY) {
			const EntityDefEnemy* ed = &def->u.enemy;
			EntityDefEnemyAnim anim = ed->anim_idle;
			switch (e->state) {
				case ENTITY_STATE_IDLE: anim = ed->anim_idle; break;
				case ENTITY_STATE_ENGAGED: anim = ed->anim_engaged; break;
				case ENTITY_STATE_ATTACK: anim = ed->anim_attack; break;
				case ENTITY_STATE_DAMAGED: anim = ed->anim_damaged; break;
				case ENTITY_STATE_DYING: anim = ed->anim_dying; break;
				case ENTITY_STATE_DEAD: anim = ed->anim_dead; break;
				default: anim = ed->anim_idle; break;
			}
			if (anim.count <= 1) {
				e->sprite_frame = (uint16_t)anim.start;
			} else {
				uint32_t f = (uint32_t)floorf(e->state_time * anim.fps);
				e->sprite_frame = (uint16_t)(anim.start + (int)(f % (uint32_t)anim.count));
			}
		}
		if (def->kind == ENTITY_KIND_PICKUP) {
			continue;
		}

		if (def->kind == ENTITY_KIND_PROJECTILE) {
			if (def->u.projectile.lifetime_s > 0.0f && e->state_time >= def->u.projectile.lifetime_s) {
				entity_system_request_despawn(es, e->id);
				continue;
			}
			if (!es->world) {
				continue;
			}
			float ang = deg_to_rad2(e->yaw_deg);
			float dir_x = cosf(ang);
			float dir_y = sinf(ang);
			float speed = def->u.projectile.speed;
			float to_x = e->body.x + dir_x * speed * dt_s;
			float to_y = e->body.y + dir_y * speed * dt_s;

			CollisionMoveResult mr = collision_move_circle(es->world, e->body.radius, e->body.x, e->body.y, to_x, to_y);
			e->body.x = mr.out_x;
			e->body.y = mr.out_y;
			// Projectiles move in XY but may have a vertical velocity (DOOM-style auto-aim).
			e->body.z += e->body.vz * dt_s;
			// Keep sector bookkeeping up to date as the projectile crosses portal boundaries.
			int sec = world_find_sector_at_point_stable(es->world, e->body.x, e->body.y, e->body.last_valid_sector);
			e->body.sector = sec;
			if (sec >= 0) {
				e->body.last_valid_sector = sec;
			}
			if (mr.collided) {
				EntityEvent ev;
				memset(&ev, 0, sizeof(ev));
				ev.type = ENTITY_EVENT_PROJECTILE_HIT_WALL;
				ev.entity = e->id;
				ev.other = entity_id_none();
				ev.def_id = e->def_id;
				ev.kind = def->kind;
				ev.x = e->body.x;
				ev.y = e->body.y;
				ev.amount = 0;
				entity_event_push(es, ev);
				entity_system_request_despawn(es, e->id);
			}
			continue;
		}

		if (def->kind == ENTITY_KIND_ENEMY) {
			if (!es->world) {
				continue;
			}
			const EntityDefEnemy* ed = &def->u.enemy;
			const PhysicsBodyParams phys = physics_body_params_default();
			float dxp = player_body->x - e->body.x;
			float dyp = player_body->y - e->body.y;
			float dist2 = dxp * dxp + dyp * dyp;
			float dist = dist2 > 0.0f ? sqrtf(dist2) : 0.0f;
			float min_approach = player_body->radius + e->body.radius + 0.05f;
			float attack_range_eff = fmaxf2(ed->attack_range, min_approach);
			// Face player for now.
			if (dist > 1e-4f) {
				e->yaw_deg = atan2f(dyp, dxp) * 180.0f / (float)M_PI;
			}

			// Death pipeline is state-driven (lets dying/dead frames show).
			if (e->hp <= 0) {
				if (e->state != ENTITY_STATE_DYING && e->state != ENTITY_STATE_DEAD) {
					e->state = ENTITY_STATE_DYING;
					e->state_time = 0.0f;
					e->attack_has_hit = false;
				}
				if (e->state == ENTITY_STATE_DYING && e->state_time >= ed->dying_time_s) {
					e->state = ENTITY_STATE_DEAD;
					e->state_time = 0.0f;
				}
				if (e->state == ENTITY_STATE_DEAD && e->state_time >= ed->dead_time_s) {
					entity_system_request_despawn(es, e->id);
				}
				continue;
			}

			if (e->state == ENTITY_STATE_DAMAGED) {
				if (e->state_time >= ed->damaged_time_s) {
					e->state = (dist <= ed->engage_range) ? ENTITY_STATE_ENGAGED : ENTITY_STATE_IDLE;
					e->state_time = 0.0f;
				}
				continue;
			}

			if (e->state == ENTITY_STATE_IDLE) {
				// Still update physics so step-down/falling works.
				physics_body_update(&e->body, es->world, 0.0f, 0.0f, (double)dt_s, &phys);
				if (dist <= ed->engage_range) {
					e->state = ENTITY_STATE_ENGAGED;
					e->state_time = 0.0f;
				}
				continue;
			}

			if (e->state == ENTITY_STATE_ENGAGED) {
				if (dist > ed->disengage_range) {
					e->state = ENTITY_STATE_IDLE;
					e->state_time = 0.0f;
					continue;
				}
				if (dist <= attack_range_eff) {
					e->state = ENTITY_STATE_ATTACK;
					e->state_time = 0.0f;
					e->attack_has_hit = false;
					continue;
				}

				// Chase
				float wish_vx = 0.0f;
				float wish_vy = 0.0f;
				if (dist > (min_approach + 0.01f) && dist > 1e-4f && ed->move_speed > 0.0f) {
					float dir_x = dxp / dist;
					float dir_y = dyp / dist;
					wish_vx = dir_x * ed->move_speed;
					wish_vy = dir_y * ed->move_speed;
				}
				// When very close to the player, block portal transitions. Otherwise, enemies can
				// end up in a different sector with a lower floor and appear to "sink".
				float near_player = fmaxf2(min_approach + 0.25f, attack_range_eff + 0.25f);
				if (dist <= near_player) {
					physics_body_update_block_portals(&e->body, es->world, wish_vx, wish_vy, (double)dt_s, &phys);
				} else {
					physics_body_update(&e->body, es->world, wish_vx, wish_vy, (double)dt_s, &phys);
				}
				// Enforce minimum distance to player to prevent clipping.
				dxp = player_body->x - e->body.x;
				dyp = player_body->y - e->body.y;
				dist2 = dxp * dxp + dyp * dyp;
				dist = dist2 > 0.0f ? sqrtf(dist2) : 0.0f;
				if (dist < min_approach) {
					float nx = 1.0f;
					float ny = 0.0f;
					if (dist > 1e-6f) {
						nx = dxp / dist;
						ny = dyp / dist;
					}
					float push = (min_approach - dist);
					physics_body_move_delta_block_portals(&e->body, es->world, -nx * push, -ny * push, &phys);
				}
				continue;
			}

			if (e->state == ENTITY_STATE_ATTACK) {
				// During attack, keep physics updated (gravity/grounding) but no intentional movement.
				physics_body_update(&e->body, es->world, 0.0f, 0.0f, (double)dt_s, &phys);
				// Keep a minimum separation during attack too.
				dxp = player_body->x - e->body.x;
				dyp = player_body->y - e->body.y;
				dist2 = dxp * dxp + dyp * dyp;
				dist = dist2 > 0.0f ? sqrtf(dist2) : 0.0f;
				if (dist < min_approach) {
					float nx = 1.0f;
					float ny = 0.0f;
					if (dist > 1e-6f) {
						nx = dxp / dist;
						ny = dyp / dist;
					}
					float push = (min_approach - dist);
					physics_body_move_delta_block_portals(&e->body, es->world, -nx * push, -ny * push, &phys);
				}
				if (!e->attack_has_hit && e->state_time >= ed->attack_windup_s) {
					float hit_range = attack_range_eff + 0.2f;
					if (dist <= hit_range && ed->attack_damage > 0) {
						EntityEvent ev;
						memset(&ev, 0, sizeof(ev));
						ev.type = ENTITY_EVENT_PLAYER_DAMAGE;
						ev.entity = e->id;
						ev.other = entity_id_none();
						ev.def_id = e->def_id;
						ev.kind = def->kind;
						ev.x = player_body->x;
						ev.y = player_body->y;
						ev.amount = ed->attack_damage;
						entity_event_push(es, ev);
					}
					e->attack_has_hit = true;
				}
				if (e->state_time >= ed->attack_cooldown_s) {
					e->state = ENTITY_STATE_ENGAGED;
					e->state_time = 0.0f;
					e->attack_has_hit = false;
				}
				continue;
			}
		}
	}

	// Rebuild spatial index using updated positions.
	spatial_rebuild(es);

	// Enemy-enemy separation (prevents clipping). Deterministic pair resolution using spatial hash.
	{
		PhysicsBodyParams phys = physics_body_params_default();
		for (uint32_t i = 0; i < es->capacity; i++) {
			if (!es->alive[i]) {
				continue;
			}
			Entity* a = &es->entities[i];
			if (a->pending_despawn) {
				continue;
			}
			const EntityDef* adef = &es->defs->defs[a->def_id];
			if (adef->kind != ENTITY_KIND_ENEMY) {
				continue;
			}
			// Query nearby candidates.
			uint32_t cand[64];
			float query_r = a->body.radius * 2.0f + 1.0f;
			uint32_t cand_count = spatial_query_circle_indices(es, a->body.x, a->body.y, query_r, cand, (uint32_t)MORTUM_ARRAY_COUNT(cand));
			// Deterministic order
			for (uint32_t k = 1; k < cand_count; k++) {
				uint32_t key = cand[k];
				int m = (int)k - 1;
				while (m >= 0 && cand[m] > key) {
					cand[m + 1] = cand[m];
					m--;
				}
				cand[m + 1] = key;
			}
			for (uint32_t ci = 0; ci < cand_count; ci++) {
				uint32_t j = cand[ci];
				if (j <= i || j >= es->capacity || !es->alive[j]) {
					continue;
				}
				Entity* b = &es->entities[j];
				if (b->pending_despawn) {
					continue;
				}
				const EntityDef* bdef = &es->defs->defs[b->def_id];
				if (bdef->kind != ENTITY_KIND_ENEMY) {
					continue;
				}
				if (a->body.sector != b->body.sector) {
					continue;
				}
				float dx = b->body.x - a->body.x;
				float dy = b->body.y - a->body.y;
				float rr = a->body.radius + b->body.radius;
				float d2 = dx * dx + dy * dy;
				if (d2 >= rr * rr || rr <= 1e-4f) {
					continue;
				}
				float d = d2 > 1e-8f ? sqrtf(d2) : 0.0f;
				float nx = 1.0f;
				float ny = 0.0f;
				if (d > 1e-6f) {
					nx = dx / d;
					ny = dy / d;
				} else {
					// Same position: deterministic axis.
					nx = ((i ^ j) & 1u) ? 1.0f : -1.0f;
					ny = 0.0f;
				}
				float push = (rr - d);
				if (push <= 0.0f) {
					continue;
				}
				float half = 0.5f * push;
				// Move both bodies using the same collision/traversal rules as everything else.
				physics_body_move_delta_block_portals(&a->body, es->world, -nx * half, -ny * half, &phys);
				physics_body_move_delta_block_portals(&b->body, es->world, nx * half, ny * half, &phys);
			}
		}
	}
	spatial_rebuild(es);

	// Pass 2: interactions via spatial queries.
	for (uint32_t i = 0; i < es->capacity; i++) {
		if (!es->alive[i]) {
			continue;
		}
		Entity* e = &es->entities[i];
		if (e->pending_despawn) {
			continue;
		}
		const EntityDef* def = &es->defs->defs[e->def_id];
		if (def->kind == ENTITY_KIND_PICKUP) {
			// Player overlap trigger.
			float dx = player_body->x - e->body.x;
			float dy = player_body->y - e->body.y;
			float r = def->u.pickup.trigger_radius + player_body->radius;
			if (dx * dx + dy * dy <= r * r) {
				EntityEvent ev;
				memset(&ev, 0, sizeof(ev));
				ev.type = ENTITY_EVENT_PLAYER_TOUCH;
				ev.entity = e->id;
				ev.other = entity_id_none();
				ev.def_id = e->def_id;
				ev.kind = def->kind;
				ev.x = e->body.x;
				ev.y = e->body.y;
				ev.amount = 0;
				entity_event_push(es, ev);
			}
			continue;
		}
		if (def->kind == ENTITY_KIND_PROJECTILE && def->u.projectile.damage > 0) {
			// Query nearby candidates (2D) and then do exact checks.
			uint32_t cand[64];
			uint32_t cand_count = spatial_query_circle_indices(es, e->body.x, e->body.y, e->body.radius + 1.0f, cand, (uint32_t)MORTUM_ARRAY_COUNT(cand));
			// Deterministic candidate order.
			for (uint32_t a = 1; a < cand_count; a++) {
				uint32_t key = cand[a];
				int b = (int)a - 1;
				while (b >= 0 && cand[b] > key) {
					cand[b + 1] = cand[b];
					b--;
				}
				cand[b + 1] = key;
			}
			for (uint32_t ci = 0; ci < cand_count; ci++) {
				uint32_t j = cand[ci];
				if (j == i || j >= es->capacity || !es->alive[j]) {
					continue;
				}
				Entity* t = &es->entities[j];
				if (t->pending_despawn) {
					continue;
				}
				if (!entity_id_is_none(e->owner) && t->id.index == e->owner.index && t->id.gen == e->owner.gen) {
					continue;
				}
				const EntityDef* tdef = &es->defs->defs[t->def_id];
				if (tdef->max_hp <= 0) {
					continue;
				}
				if (tdef->kind == ENTITY_KIND_PICKUP || tdef->kind == ENTITY_KIND_PROJECTILE) {
					continue;
				}
				if (t->body.sector != e->body.sector) {
					// Allow cross-sector damage through portals/open spaces, but prevent
					// damaging through solid walls.
					if (e->body.sector < 0 || t->body.sector < 0) {
						continue;
					}
					if (!collision_line_of_sight(es->world, e->body.x, e->body.y, t->body.x, t->body.y)) {
						continue;
					}
				}
				float dx = t->body.x - e->body.x;
				float dy = t->body.y - e->body.y;
				float rr = t->body.radius + e->body.radius;
				if (dx * dx + dy * dy > rr * rr) {
					continue;
				}
				float ez0 = e->body.z;
				float ez1 = e->body.z + e->body.height;
				float tz0 = t->body.z;
				float tz1 = t->body.z + t->body.height;
				if (ez1 < tz0 || tz1 < ez0) {
					continue;
				}

				EntityEvent ev;
				memset(&ev, 0, sizeof(ev));
				ev.type = ENTITY_EVENT_DAMAGE;
				ev.entity = e->id; // source
				ev.other = t->id; // target
				ev.def_id = e->def_id;
				ev.kind = def->kind;
				ev.x = t->body.x;
				ev.y = t->body.y;
				ev.amount = def->u.projectile.damage;
				entity_event_push(es, ev);
				entity_system_request_despawn(es, e->id);
				break;
			}
			continue;
		}
	}
}

void entity_system_resolve_player_collisions(EntitySystem* es, PhysicsBody* player_body) {
	if (!es || !player_body || !es->world || !es->defs) {
		return;
	}
	PhysicsBodyParams phys = physics_body_params_default();

	// A few passes helps resolve multi-overlap without oscillation.
	for (int pass = 0; pass < 3; pass++) {
		bool any = false;
		for (uint32_t i = 0; i < es->capacity; i++) {
			if (!es->alive[i]) {
				continue;
			}
			Entity* e = &es->entities[i];
			if (e->pending_despawn) {
				continue;
			}
			const EntityDef* def = &es->defs->defs[e->def_id];
			if (def->kind != ENTITY_KIND_ENEMY) {
				continue;
			}
			float dx = e->body.x - player_body->x;
			float dy = e->body.y - player_body->y;
			float min_d = e->body.radius + player_body->radius;
			if (min_d <= 1e-6f) {
				continue;
			}
			float d2 = dx * dx + dy * dy;
			float min2 = min_d * min_d;
			if (d2 >= min2) {
				continue;
			}
			any = true;
			float d = d2 > 1e-8f ? sqrtf(d2) : 0.0f;
			float nx = 1.0f;
			float ny = 0.0f;
			if (d > 1e-6f) {
				nx = dx / d;
				ny = dy / d;
			} else {
				// Deterministic axis for exact overlap.
				nx = (i & 1u) ? 1.0f : -1.0f;
				ny = 0.0f;
			}
			float push = (min_d - d);
			if (push <= 0.0f) {
				continue;
			}
			float half = 0.5f * push;
			// Push both bodies apart.
			physics_body_move_delta_block_portals(player_body, es->world, -nx * half, -ny * half, &phys);
			physics_body_move_delta_block_portals(&e->body, es->world, nx * half, ny * half, &phys);
		}
		if (!any) {
			break;
		}
	}
}

static uint32_t spatial_hash_i32(int x, int y) {
	// Deterministic integer hash (good enough for a small bucket table).
	uint32_t ux = (uint32_t)x;
	uint32_t uy = (uint32_t)y;
	return (ux * 73856093u) ^ (uy * 19349663u);
}

static void spatial_invalidate(EntitySystem* es) {
	if (!es) {
		return;
	}
	es->spatial_valid = false;
}

static void spatial_rebuild(EntitySystem* es) {
	if (!es || !es->spatial_head || !es->spatial_next || es->spatial_bucket_count == 0u) {
		return;
	}
	for (uint32_t bi = 0; bi < es->spatial_bucket_count; bi++) {
		es->spatial_head[bi] = UINT32_MAX;
	}
	float cs = es->spatial_cell_size;
	if (cs <= 0.0f) {
		cs = 1.0f;
		es->spatial_cell_size = cs;
	}
	for (uint32_t i = 0; i < es->capacity; i++) {
		if (!es->alive[i]) {
			continue;
		}
		Entity* e = &es->entities[i];
		if (e->pending_despawn) {
			continue;
		}
		int cx = (int)floorf(e->body.x / cs);
		int cy = (int)floorf(e->body.y / cs);
		uint32_t h = spatial_hash_i32(cx, cy);
		uint32_t b = h & (es->spatial_bucket_count - 1u);
		es->spatial_next[i] = es->spatial_head[b];
		es->spatial_head[b] = i;
	}
	es->spatial_valid = true;
}

static uint32_t spatial_query_circle_indices(EntitySystem* es, float x, float y, float radius, uint32_t* out_idx, uint32_t out_cap) {
	if (!es || !out_idx || out_cap == 0u) {
		return 0u;
	}
	if (!es->spatial_valid) {
		spatial_rebuild(es);
	}
	if (!es->spatial_valid) {
		return 0u;
	}
	if (!es->spatial_seen) {
		return 0u;
	}

	// Stamp handling (0 means "never seen")
	es->spatial_stamp++;
	if (es->spatial_stamp == 0u) {
		memset(es->spatial_seen, 0, (size_t)es->capacity * sizeof(uint32_t));
		es->spatial_stamp = 1u;
	}
	uint32_t stamp = es->spatial_stamp;

	float cs = es->spatial_cell_size;
	if (cs <= 0.0f) {
		cs = 1.0f;
	}
	float r = radius;
	if (r < 0.0f) {
		r = 0.0f;
	}
	float r2 = r * r;
	int min_cx = (int)floorf((x - r) / cs);
	int max_cx = (int)floorf((x + r) / cs);
	int min_cy = (int)floorf((y - r) / cs);
	int max_cy = (int)floorf((y + r) / cs);

	uint32_t count = 0u;
	for (int cy = min_cy; cy <= max_cy; cy++) {
		for (int cx = min_cx; cx <= max_cx; cx++) {
			uint32_t h = spatial_hash_i32(cx, cy);
			uint32_t b = h & (es->spatial_bucket_count - 1u);
			uint32_t idx = es->spatial_head[b];
			while (idx != UINT32_MAX) {
				uint32_t next = es->spatial_next[idx];
				if (idx < es->capacity && es->alive[idx] && !es->entities[idx].pending_despawn) {
					if (es->spatial_seen[idx] != stamp) {
						es->spatial_seen[idx] = stamp;
						float dx = es->entities[idx].body.x - x;
						float dy = es->entities[idx].body.y - y;
						if (dx * dx + dy * dy <= r2) {
							if (count < out_cap) {
								out_idx[count++] = idx;
							}
						}
					}
				}
				idx = next;
			}
		}
	}
	return count;
}

uint32_t entity_system_query_circle(EntitySystem* es, float x, float y, float radius, EntityId* out_ids, uint32_t out_cap) {
	if (!es || !out_ids || out_cap == 0u) {
		return 0u;
	}
	uint32_t tmp[128];
	uint32_t n = spatial_query_circle_indices(es, x, y, radius, tmp, (uint32_t)MORTUM_ARRAY_COUNT(tmp));
	uint32_t out_n = n < out_cap ? n : out_cap;
	for (uint32_t i = 0; i < out_n; i++) {
		out_ids[i] = es->entities[tmp[i]].id;
	}
	return out_n;
}

uint32_t entity_system_alive_count(const EntitySystem* es) {
	return es ? es->alive_count : 0u;
}

typedef struct SpriteDrawItem {
	float depth;
	const Entity* e;
	const EntityDef* def;
} SpriteDrawItem;

static float deg_to_rad2(float deg) {
	return deg * (float)M_PI / 180.0f;
}

static float clampf2(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static void sort_sprites_back_to_front(SpriteDrawItem* items, int n) {
	// Stable insertion sort (deterministic for equal depths).
	for (int i = 1; i < n; i++) {
		SpriteDrawItem key = items[i];
		int j = i - 1;
		while (j >= 0 && items[j].depth < key.depth) {
			items[j + 1] = items[j];
			j--;
		}
		items[j + 1] = key;
	}
}

static float camera_world_z_for_sector_approx(const World* world, int sector, float z_offset) {
	// Keep in sync with render/raycast.c constants.
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

void entity_system_draw_sprites(const EntitySystem* es, Framebuffer* fb, const World* world, const Camera* cam, int start_sector, TextureRegistry* texreg, const AssetPaths* paths, const float* wall_depth) {
	if (!es || !fb || !fb->pixels || !world || !cam || !texreg || !paths || !es->defs) {
		return;
	}
	if (!wall_depth) {
		return;
	}

	// Gather visible sprite entities.
	SpriteDrawItem items[256];
	int count = 0;

	float cam_rad = deg_to_rad2(cam->angle_deg);
	float fx = cosf(cam_rad);
	float fy = sinf(cam_rad);
	float rx = -fy;
	float ry = fx;
	float fov_rad = deg_to_rad2(cam->fov_deg);
	float half_w = 0.5f * (float)fb->width;
	float half_h = 0.5f * (float)fb->height;
	float tan_half_fov = tanf(0.5f * fov_rad);
	if (tan_half_fov < 1e-4f) {
		return;
	}
	float focal = half_w / tan_half_fov; // pixels

	float cam_z_world = camera_world_z_for_sector_approx(world, start_sector, cam->z);

	for (uint32_t i = 0; i < es->capacity; i++) {
		if (!es->alive[i]) {
			continue;
		}
		const Entity* e = &es->entities[i];
		const EntityDef* def = &es->defs->defs[e->def_id];
		if (def->sprite.file.name[0] == '\0') {
			continue;
		}
		float dx = e->body.x - cam->x;
		float dy = e->body.y - cam->y;
		float depth = dx * fx + dy * fy;
		if (depth <= 0.05f) {
			continue;
		}
		if (count < (int)MORTUM_ARRAY_COUNT(items)) {
			items[count].depth = depth;
			items[count].e = e;
			items[count].def = def;
			count++;
		}
	}

	if (count <= 0) {
		return;
	}

	sort_sprites_back_to_front(items, count);

	for (int si = 0; si < count; si++) {
		const Entity* e = items[si].e;
		const EntityDef* def = items[si].def;
		const Texture* tex = texture_registry_get(texreg, paths, def->sprite.file.name);
		if (!tex || !tex->pixels) {
			continue;
		}
		if (def->sprite.file.width > 0 && def->sprite.file.height > 0) {
			if (tex->width != def->sprite.file.width || tex->height != def->sprite.file.height) {
				log_warn(
					"sprite '%s' size mismatch: def says %dx%d, texture is %dx%d",
					def->sprite.file.name,
					def->sprite.file.width,
					def->sprite.file.height,
					tex->width,
					tex->height
				);
			}
		}

		// Resolve effective sheet dimensions: prefer actual loaded texture size.
		int sheet_w = tex->width;
		int sheet_h = tex->height;
		int frame_w = def->sprite.frames.width > 0 ? def->sprite.frames.width : sheet_w;
		int frame_h = def->sprite.frames.height > 0 ? def->sprite.frames.height : sheet_h;
		int frame_count = def->sprite.frames.count > 0 ? def->sprite.frames.count : 1;
		if (frame_w <= 0 || frame_h <= 0 || sheet_w <= 0 || sheet_h <= 0) {
			continue;
		}
		if (frame_count < 1) {
			frame_count = 1;
		}
		int frame_index = (int)e->sprite_frame;
		if (frame_index >= frame_count) {
			frame_index = frame_count - 1;
		}
		int frame_x = frame_index * frame_w;
		int frame_y = 0;
		if (frame_x + frame_w > sheet_w || frame_y + frame_h > sheet_h) {
			continue;
		}

		float dx = e->body.x - cam->x;
		float dy = e->body.y - cam->y;
		float depth = dx * fx + dy * fy;
		float side = dx * rx + dy * ry;
		// Projection scale used for sprites. Key rule: if we clamp the *size* for stability,
		// we must also clamp the *projection scale* (pixels-per-world-unit) consistently.
		// Otherwise, when a sprite hits a max pixel size, it can keep shifting vertically
		// (screen-space "sinking") without scaling, which looks unphysical.
		float proj_depth = depth;
		const float min_proj_depth = 0.25f;
		if (proj_depth < min_proj_depth) {
			proj_depth = min_proj_depth;
		}

		// Convention: 64px == 1 world unit at scale=1.
		float sprite_w_world = ((float)frame_w / 64.0f) * def->sprite.scale;
		float sprite_h_world = ((float)frame_h / 64.0f) * def->sprite.scale;
		if (sprite_w_world <= 1e-6f || sprite_h_world <= 1e-6f) {
			continue;
		}

		float scale = focal / proj_depth; // px per world unit
		// Clamp projected size uniformly by clamping scale itself.
		int max_w = fb->width * 2;
		int max_h = fb->height * 2;
		if (max_w > 0 && max_h > 0) {
			float max_scale_w = (float)max_w / sprite_w_world;
			float max_scale_h = (float)max_h / sprite_h_world;
			float max_scale = max_scale_w < max_scale_h ? max_scale_w : max_scale_h;
			if (max_scale > 1e-6f && scale > max_scale) {
				scale = max_scale;
			}
		}

		// Screen position and pixel size (all derived from the same scale).
		float x_center = half_w + side * scale;
		int sprite_w_px = (int)(sprite_w_world * scale + 0.5f);
		int sprite_h_px = (int)(sprite_h_world * scale + 0.5f);
		if (sprite_w_px < 2) {
			sprite_w_px = 2;
		}
		if (sprite_h_px < 2) {
			sprite_h_px = 2;
		}
		if (sprite_w_px <= 1 || sprite_h_px <= 1) {
			continue;
		}

		// Vertical placement: put sprite base at entity feet + z_offset above floor.
		float ent_z = e->body.z + (def->sprite.z_offset / 64.0f);
		float y_base = half_h + (cam_z_world - ent_z) * scale;
		int x0 = (int)(x_center - 0.5f * (float)sprite_w_px);
		int x1 = x0 + sprite_w_px;
		int y1 = (int)y_base;
		int y0 = y1 - sprite_h_px;

		// Clip.
		int clip_x0 = x0 < 0 ? 0 : x0;
		int clip_x1 = x1 > fb->width ? fb->width : x1;
		int clip_y0 = y0 < 0 ? 0 : y0;
		int clip_y1 = y1 > fb->height ? fb->height : y1;
		if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) {
			continue;
		}

		for (int x = clip_x0; x < clip_x1; x++) {
			// Wall occlusion check per column.
			if (depth >= wall_depth[x]) {
				continue;
			}
			float u = (float)(x - x0) / (float)(sprite_w_px - 1);
			u = clampf2(u, 0.0f, 1.0f);
			float tex_u = (float)(frame_x) / (float)(sheet_w - 1);
			float tex_u_span = (float)(frame_w - 1) / (float)(sheet_w - 1);
			tex_u = tex_u + u * tex_u_span;
			for (int y = clip_y0; y < clip_y1; y++) {
				float v = (float)(y - y0) / (float)(sprite_h_px - 1);
				v = clampf2(v, 0.0f, 1.0f);
				float tex_v = (float)(frame_y) / (float)(sheet_h - 1);
				float tex_v_span = (float)(frame_h - 1) / (float)(sheet_h - 1);
				tex_v = tex_v + v * tex_v_span;
				uint32_t c = texture_sample_nearest(tex, tex_u, tex_v);
				// Global sprite colorkey: FF00FF (magenta) is transparent.
				if ((c & 0x00FFFFFFu) == 0x00FF00FFu) {
					continue;
				}
				if ((c & 0xFF000000u) == 0u) {
					continue;
				}
				fb->pixels[y * fb->width + x] = c;
			}
		}
	}
}
