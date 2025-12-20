#include "render/entities.h"

#include "render/draw.h"

#include "render/lighting.h"

#include "render/texture.h"

#include <math.h>
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

static float wrap_pi(float a) {
	while (a > (float)M_PI) {
		a -= 2.0f * (float)M_PI;
	}
	while (a < -(float)M_PI) {
		a += 2.0f * (float)M_PI;
	}
	return a;
}

static uint32_t entity_color(const char* type) {
	if (!type) {
		return 0xFFFFFFFFu;
	}
	if (strcmp(type, "pickup_health") == 0) {
		return 0xFFFF4040u;
	}
	if (strcmp(type, "pickup_ammo") == 0) {
		return 0xFFFFD040u;
	}
	if (strcmp(type, "pickup_key") == 0) {
		return 0xFF40E0FFu;
	}
	if (strcmp(type, "gate_key") == 0) {
		return 0xFF40A0FFu;
	}
	if (strcmp(type, "exit") == 0) {
		return 0xFF60FF60u;
	}
	if (strcmp(type, "enemy_melee") == 0) {
		return 0xFFFF40FFu;
	}
	if (strcmp(type, "enemy_turret") == 0) {
		return 0xFFFF9040u;
	}
	if (strcmp(type, "proj_player") == 0) {
		return 0xFFFFFFFFu;
	}
	if (strcmp(type, "proj_enemy") == 0) {
		return 0xFFFF8080u;
	}
	return 0xFFE0E0E0u;
}

static bool entity_sprite_tile(const char* type, float cooldown_s, int* out_tx, int* out_ty) {
	if (!type) {
		return false;
	}
	// Assumes `sprites.bmp` is a grid of 64x64 sprites.
	// Tile mapping per provided atlas metadata (cols/rows are 0-based).
	if (strcmp(type, "pickup_health") == 0) {
		// pickup_health_small
		*out_tx = 0;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_ammo") == 0) {
		// pickup_ammo_box
		*out_tx = 6;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_ammo_bullets") == 0 || strcmp(type, "pickup_ammo_shells") == 0 || strcmp(type, "pickup_ammo_cells") == 0) {
		// reuse pickup_ammo_box
		*out_tx = 6;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_weapon_shotgun") == 0 || strcmp(type, "pickup_weapon_rifle") == 0 || strcmp(type, "pickup_weapon_chaingun") == 0) {
		// reuse pickup_ammo_box for weapon stand-ins (keeps arena readable without new art)
		*out_tx = 6;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_upgrade_max_health") == 0) {
		// reuse pickup_health_small
		*out_tx = 0;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_upgrade_max_ammo") == 0) {
		// reuse pickup_ammo_box
		*out_tx = 6;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_toxic_medkit") == 0) {
		*out_tx = 1;
		*out_ty = 5;
		return true;
	}
	if (strcmp(type, "pickup_shard") == 0) {
		// item_corrupt_crystal
		*out_tx = 4;
		*out_ty = 6;
		return true;
	}
	if (strcmp(type, "pickup_mortum_small") == 0) {
		*out_tx = 4;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_mortum_large") == 0) {
		*out_tx = 5;
		*out_ty = 4;
		return true;
	}
	if (strcmp(type, "pickup_key") == 0) {
		// key_gold
		*out_tx = 0;
		*out_ty = 7;
		return true;
	}
	if (strcmp(type, "enemy_melee") == 0) {
		// enemy_crawler
		*out_tx = 0;
		*out_ty = 3;
		return true;
	}
	if (strcmp(type, "enemy_turret") == 0) {
		// enemy_bone_turret_idle / enemy_bone_turret_fire
		// Simple flash right after firing (cooldown is reset to ~1.10s when firing).
		if (cooldown_s > 0.90f) {
			*out_tx = 8;
			*out_ty = 1;
			return true;
		}
		*out_tx = 7;
		*out_ty = 1;
		return true;
	}
	if (strcmp(type, "gate_key") == 0) {
		// marker_locked_door
		*out_tx = 6;
		*out_ty = 7;
		return true;
	}
	if (strcmp(type, "exit") == 0) {
		// portal_exit
		*out_tx = 7;
		*out_ty = 7;
		return true;
	}
	if (strcmp(type, "proj_player") == 0) {
		// proj_fire_small
		*out_tx = 4;
		*out_ty = 0;
		return true;
	}
	if (strcmp(type, "proj_enemy") == 0) {
		// proj_fire_medium
		*out_tx = 5;
		*out_ty = 0;
		return true;
	}
	if (strcmp(type, "hazard_wall_growth") == 0) {
		*out_tx = 7;
		*out_ty = 3;
		return true;
	}
	if (strcmp(type, "hazard_floor_growth") == 0) {
		*out_tx = 8;
		*out_ty = 3;
		return true;
	}
	return false;
}

static uint32_t texture_sample_tile_nearest(const Texture* t, int tile_x, int tile_y, int tile_size, float u, float v) {
	if (!t || !t->pixels || t->width <= 0 || t->height <= 0) {
		return 0xFFFF00FFu;
	}
	u = clampf(u, 0.0f, 1.0f);
	v = clampf(v, 0.0f, 1.0f);

	int x0 = tile_x * tile_size;
	int y0 = tile_y * tile_size;
	int x = x0 + (int)(u * (float)(tile_size - 1) + 0.5f);
	int y = y0 + (int)(v * (float)(tile_size - 1) + 0.5f);

	if ((unsigned)x >= (unsigned)t->width || (unsigned)y >= (unsigned)t->height) {
		return 0xFFFF00FFu;
	}
	return t->pixels[y * t->width + x];
}

typedef struct BillboardCandidate {
	const Entity* e;
	float perp;
	int x0;
	int y0;
	int sprite_w;
	int sprite_h;
	uint32_t col;
	int tile_x;
	int tile_y;
	bool has_tile;
} BillboardCandidate;

static int billboard_cmp_far_to_near(const void* a, const void* b) {
	const BillboardCandidate* ca = (const BillboardCandidate*)a;
	const BillboardCandidate* cb = (const BillboardCandidate*)b;
	if (ca->perp > cb->perp) {
		return -1;
	}
	if (ca->perp < cb->perp) {
		return 1;
	}
	return 0;
}

void render_entities_billboard(
	Framebuffer* fb,
	const Camera* cam,
	const EntityList* entities,
	const float* depth,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const PointLight* lights,
	int light_count) {
	if (!fb || !cam || !entities) {
		return;
	}

	const Texture* sprites = NULL;
	if (texreg && paths) {
		sprites = texture_registry_get(texreg, paths, "sprites.bmp");
	}

	float fov_rad = cam->fov_deg * (float)M_PI / 180.0f;
	if (fov_rad < 1e-5f) {
		return;
	}
	float half_fov = 0.5f * fov_rad;
	float cam_ang = cam->angle_deg * (float)M_PI / 180.0f;

	static BillboardCandidate* cands = NULL;
	static int cands_cap = 0;
	int cand_count = 0;

	for (int i = 0; i < entities->count; i++) {
		const Entity* e = &entities->items[i];
		if (!e->active) {
			continue;
		}

		// Don't billboard non-world entities that would be noisy.
		if (strncmp(e->type, "pickup_", 7) != 0 && strncmp(e->type, "enemy_", 6) != 0 && strcmp(e->type, "gate_key") != 0 &&
			strcmp(e->type, "exit") != 0 && strcmp(e->type, "proj_player") != 0 && strcmp(e->type, "proj_enemy") != 0) {
			continue;
		}

		float dx = e->x - cam->x;
		float dy = e->y - cam->y;
		float dist = sqrtf(dx * dx + dy * dy);
		if (dist < 1e-4f) {
			continue;
		}

		float ang_to = atan2f(dy, dx);
		float rel = wrap_pi(ang_to - cam_ang);
		if (fabsf(rel) > half_fov) {
			continue;
		}

		// Perpendicular distance for correct size & occlusion compare.
		float perp = dist * cosf(rel);
		if (perp <= 1e-4f) {
			continue;
		}

		float ndc_x = (rel / half_fov); // -1..1
		float screen_x = ((ndc_x + 1.0f) * 0.5f) * (float)fb->width;

		// Size: tune per class (simple, readable).
		float world_h = 0.75f;
		if (strncmp(e->type, "enemy_", 6) == 0) {
			world_h = 0.95f;
		} else if (strncmp(e->type, "pickup_", 7) == 0) {
			world_h = 0.55f;
		} else if (strncmp(e->type, "proj_", 5) == 0) {
			world_h = 0.20f;
		}

		int sprite_h = (int)((world_h * (float)fb->height) / (perp + 0.001f));
		sprite_h = (int)clampf((float)sprite_h, 2.0f, (float)fb->height);
		int sprite_w = sprite_h;

		int cx = (int)screen_x;
		int x0 = cx - sprite_w / 2;
		int y0 = (fb->height - sprite_h) / 2;

		uint32_t col = entity_color(e->type);
		int tile_x = 0;
		int tile_y = 0;
		bool has_tile = sprites ? entity_sprite_tile(e->type, e->cooldown_s, &tile_x, &tile_y) : false;

		if (cand_count >= cands_cap) {
			int new_cap = cands_cap > 0 ? (cands_cap * 2) : 64;
			BillboardCandidate* new_buf = (BillboardCandidate*)realloc(cands, (size_t)new_cap * sizeof(*new_buf));
			if (!new_buf) {
				// Out of memory: render the candidates we already collected.
				break;
			}
			cands = new_buf;
			cands_cap = new_cap;
		}

		cands[cand_count].e = e;
		cands[cand_count].perp = perp;
		cands[cand_count].x0 = x0;
		cands[cand_count].y0 = y0;
		cands[cand_count].sprite_w = sprite_w;
		cands[cand_count].sprite_h = sprite_h;
		cands[cand_count].col = col;
		cands[cand_count].tile_x = tile_x;
		cands[cand_count].tile_y = tile_y;
		cands[cand_count].has_tile = has_tile;
		cand_count++;
	}

	if (!cands || cand_count <= 0) {
		return;
	}

	qsort(cands, (size_t)cand_count, sizeof(*cands), billboard_cmp_far_to_near);

	for (int i = 0; i < cand_count; i++) {
		const BillboardCandidate* c = &cands[i];
		const Entity* e = c->e;
		LightColor sector_tint = light_color_white();

		// Draw as vertical strips to support depth-buffer occlusion.
		for (int sx = 0; sx < c->sprite_w; sx++) {
			int x = c->x0 + sx;
			if ((unsigned)x >= (unsigned)fb->width) {
				continue;
			}
			if (depth && c->perp > depth[x]) {
				continue;
			}

			if (!sprites || !c->has_tile) {
				// Fallback: solid color column.
				uint32_t col = lighting_apply(c->col, c->perp, 1.0f, sector_tint, lights, light_count, e->x, e->y);
				draw_rect(fb, x, c->y0, 1, c->sprite_h, col);
				continue;
			}

			float u = (float)sx / (float)(c->sprite_w > 1 ? (c->sprite_w - 1) : 1);
			for (int sy = 0; sy < c->sprite_h; sy++) {
				int y = c->y0 + sy;
				if ((unsigned)y >= (unsigned)fb->height) {
					continue;
				}
				float v = (float)sy / (float)(c->sprite_h > 1 ? (c->sprite_h - 1) : 1);
				uint32_t px = texture_sample_tile_nearest(sprites, c->tile_x, c->tile_y, 64, u, v);
				// Magenta color key transparency.
				if (px == 0xFFFF00FFu) {
					continue;
				}
				px = lighting_apply(px, c->perp, 1.0f, sector_tint, lights, light_count, e->x, e->y);
				fb->pixels[y * fb->width + x] = px;
			}
		}
	}
}
