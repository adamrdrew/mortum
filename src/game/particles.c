#include "game/particles.h"

#include "assets/asset_paths.h"
#include "core/base.h"
#include "core/log.h"
#include "game/world.h"
#include "render/camera.h"
#include "render/framebuffer.h"
#include "render/texture.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static inline float clampf2(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static inline float lerpf(float a, float b, float t) {
	return a + (b - a) * t;
}

static inline uint32_t pack_abgr_u8(uint8_t a, uint8_t b, uint8_t g, uint8_t r) {
	return ((uint32_t)a << 24u) | ((uint32_t)b << 16u) | ((uint32_t)g << 8u) | (uint32_t)r;
}

static inline uint32_t blend_abgr8888_over(uint32_t src, uint32_t dst) {
	uint32_t sa = (src >> 24u) & 0xFFu;
	if (sa == 0u) {
		return dst;
	}
	if (sa == 255u) {
		return src;
	}
	uint32_t inv = 255u - sa;
	uint32_t sb = (src >> 16u) & 0xFFu;
	uint32_t sg = (src >> 8u) & 0xFFu;
	uint32_t sr = src & 0xFFu;
	uint32_t da = (dst >> 24u) & 0xFFu;
	uint32_t db = (dst >> 16u) & 0xFFu;
	uint32_t dg = (dst >> 8u) & 0xFFu;
	uint32_t dr = dst & 0xFFu;
	uint32_t oa = sa + (da * inv + 127u) / 255u;
	uint32_t ob = (sb * sa + db * inv + 127u) / 255u;
	uint32_t og = (sg * sa + dg * inv + 127u) / 255u;
	uint32_t or_ = (sr * sa + dr * inv + 127u) / 255u;
	return (oa << 24u) | (ob << 16u) | (og << 8u) | or_;
}

bool particles_init(Particles* self, int capacity) {
	if (!self) {
		return false;
	}
	memset(self, 0, sizeof(*self));
	if (capacity <= 0) {
		capacity = PARTICLE_MAX_DEFAULT;
	}
	self->items = (Particle*)calloc((size_t)capacity, sizeof(Particle));
	if (!self->items) {
		return false;
	}
	self->capacity = capacity;
	self->initialized = true;
	return true;
}

void particles_shutdown(Particles* self) {
	if (!self) {
		return;
	}
	free(self->items);
	memset(self, 0, sizeof(*self));
}

void particles_reset(Particles* self) {
	if (!self || !self->initialized) {
		return;
	}
	memset(self->items, 0, (size_t)self->capacity * sizeof(Particle));
	self->alive_count = 0;
}

void particles_tick(Particles* self, uint32_t dt_ms) {
	if (!self || !self->initialized || !self->items) {
		return;
	}
	if (dt_ms == 0u) {
		return;
	}
	int alive = 0;
	for (int i = 0; i < self->capacity; i++) {
		Particle* p = &self->items[i];
		if (!p->alive) {
			continue;
		}
		p->age_ms += dt_ms;
		if (p->life_ms == 0u || p->age_ms >= p->life_ms) {
			p->alive = false;
			continue;
		}
		if (p->rotate_enabled && p->rot_step_ms > 0u && p->rot_step_deg != 0.0f) {
			p->rot_accum_ms += dt_ms;
			while (p->rot_accum_ms >= p->rot_step_ms) {
				p->rot_accum_ms -= p->rot_step_ms;
				p->rot_deg += p->rot_step_deg;
				// Keep angle bounded.
				if (p->rot_deg >= 360.0f || p->rot_deg <= -360.0f) {
					p->rot_deg = fmodf(p->rot_deg, 360.0f);
				}
			}
		}
		alive++;
	}
	self->alive_count = alive;
}

void particles_spawn(Particles* self, const Particle* p) {
	if (!self || !self->initialized || !self->items || !p) {
		return;
	}
	// Drop newest when full.
	if (self->alive_count >= self->capacity) {
		return;
	}
	for (int i = 0; i < self->capacity; i++) {
		if (!self->items[i].alive) {
			self->items[i] = *p;
			self->items[i].alive = true;
			self->alive_count++;
			return;
		}
	}
}

static inline uint32_t mul_alpha_u8(uint32_t abgr, uint8_t a_mul) {
	uint32_t a = (abgr >> 24u) & 0xFFu;
	a = (a * (uint32_t)a_mul + 127u) / 255u;
	return (abgr & 0x00FFFFFFu) | (a << 24u);
}

static void lerp_keyframe(const Particle* p, float t, ParticleKeyframe* out) {
	*out = p->start;
	out->opacity = lerpf(p->start.opacity, p->end.opacity, t);
	out->size = lerpf(p->start.size, p->end.size, t);
	out->r = lerpf(p->start.r, p->end.r, t);
	out->g = lerpf(p->start.g, p->end.g, t);
	out->b = lerpf(p->start.b, p->end.b, t);
	out->color_blend_opacity = lerpf(p->start.color_blend_opacity, p->end.color_blend_opacity, t);
	out->off_x = lerpf(p->start.off_x + p->jitter_start_x, p->end.off_x + p->jitter_end_x, t);
	out->off_y = lerpf(p->start.off_y + p->jitter_start_y, p->end.off_y + p->jitter_end_y, t);
	out->off_z = lerpf(p->start.off_z + p->jitter_start_z, p->end.off_z + p->jitter_end_z, t);
	out->opacity = clampf2(out->opacity, 0.0f, 1.0f);
	out->size = fmaxf(out->size, 0.0f);
	out->r = clampf2(out->r, 0.0f, 1.0f);
	out->g = clampf2(out->g, 0.0f, 1.0f);
	out->b = clampf2(out->b, 0.0f, 1.0f);
	out->color_blend_opacity = clampf2(out->color_blend_opacity, 0.0f, 1.0f);
}

static inline float deg_to_rad2(float deg) {
	return deg * (float)M_PI / 180.0f;
}

static float camera_world_z_for_sector_approx2(const World* world, int sector, float z_offset) {
	if (!world || (unsigned)sector >= (unsigned)world->sector_count) {
		return z_offset;
	}
	return world->sectors[sector].floor_z + z_offset;
}

void particles_draw(
	const Particles* self,
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	int start_sector,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const float* wall_depth,
	const float* depth_pixels) {
	if (!self || !self->initialized || !self->items || !fb || !fb->pixels || !world || !cam || !texreg || !paths) {
		return;
	}
	if (!wall_depth && !depth_pixels) {
		return;
	}

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
	float focal = half_w / tan_half_fov;

	float cam_z_world = 0.0f;
	if ((unsigned)start_sector < (unsigned)world->sector_count) {
		cam_z_world = camera_world_z_for_sector_approx2(world, start_sector, cam->z);
	}

	for (int i = 0; i < self->capacity; i++) {
		const Particle* p = &self->items[i];
		if (!p->alive) {
			continue;
		}
		float t = (p->life_ms > 0u) ? ((float)p->age_ms / (float)p->life_ms) : 1.0f;
		t = clampf2(t, 0.0f, 1.0f);
		ParticleKeyframe k;
		lerp_keyframe(p, t, &k);
		if (k.opacity <= 0.001f || k.size <= 1e-6f) {
			continue;
		}

		float px = p->origin_x + k.off_x;
		float py = p->origin_y + k.off_y;
		float pz = p->origin_z + k.off_z;

		float dx = px - cam->x;
		float dy = py - cam->y;
		float depth = dx * fx + dy * fy;
		if (depth <= 0.05f) {
			continue;
		}
		float side = dx * rx + dy * ry;

		float proj_depth = depth;
		const float min_proj_depth = 0.25f;
		if (proj_depth < min_proj_depth) {
			proj_depth = min_proj_depth;
		}

		float scale = focal / proj_depth;

		int w_px = (int)(k.size * scale + 0.5f);
		int h_px = w_px;
		if (w_px < 2) {
			w_px = 2;
			h_px = 2;
		}
		int max_w = fb->width * 2;
		int max_h = fb->height * 2;
		if (max_w > 0 && max_h > 0) {
			if (w_px > max_w) {
				w_px = max_w;
				h_px = max_h < w_px ? max_h : w_px;
			}
		}

		float x_center = half_w + side * scale;
		float y_center = half_h + (cam_z_world - pz) * scale;

		int x0 = (int)(x_center - 0.5f * (float)w_px);
		int x1 = x0 + w_px;
		int y0 = (int)(y_center - 0.5f * (float)h_px);
		int y1 = y0 + h_px;

		int clip_x0 = x0 < 0 ? 0 : x0;
		int clip_x1 = x1 > fb->width ? fb->width : x1;
		int clip_y0 = y0 < 0 ? 0 : y0;
		int clip_y1 = y1 > fb->height ? fb->height : y1;
		if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) {
			continue;
		}

		const Texture* tex = NULL;
		int tex_w = 0;
		int tex_h = 0;
		if (p->has_image && p->image[0] != '\0') {
			tex = texture_registry_get(texreg, paths, p->image);
			if (!tex || !tex->pixels || tex->width <= 0 || tex->height <= 0) {
				tex = NULL;
			} else {
				tex_w = tex->width;
				tex_h = tex->height;
			}
		}

		float rot_rad = p->rotate_enabled ? deg_to_rad2(-p->rot_deg) : 0.0f; // clockwise
		float cr = cosf(rot_rad);
		float sr = sinf(rot_rad);

		uint8_t out_a = (uint8_t)lroundf(clampf2(k.opacity, 0.0f, 1.0f) * 255.0f);
		uint8_t tint_r = (uint8_t)lroundf(k.r * 255.0f);
		uint8_t tint_g = (uint8_t)lroundf(k.g * 255.0f);
		uint8_t tint_b = (uint8_t)lroundf(k.b * 255.0f);

		for (int x = clip_x0; x < clip_x1; x++) {
			if (wall_depth && depth >= wall_depth[x]) {
				continue;
			}
			for (int y = clip_y0; y < clip_y1; y++) {
				if (depth_pixels) {
					float world_depth = depth_pixels[y * fb->width + x];
					if (depth >= world_depth) {
						continue;
					}
				}

				// Local coords in [-0.5,0.5] range.
				float u = (float)(x - x0) / (float)(w_px - 1);
				float v = (float)(y - y0) / (float)(h_px - 1);
				u = clampf2(u, 0.0f, 1.0f);
				v = clampf2(v, 0.0f, 1.0f);
				float lx = (u - 0.5f);
				float ly = (v - 0.5f);

				// Apply rotation for squares/images; circles ignore rotation.
				float ru = u;
				float rv = v;
				if (p->rotate_enabled && (tex || p->shape == PARTICLE_SHAPE_SQUARE)) {
					float rxu = lx * cr - ly * sr;
					float ryu = lx * sr + ly * cr;
					ru = rxu + 0.5f;
					rv = ryu + 0.5f;
					if (ru < 0.0f || ru > 1.0f || rv < 0.0f || rv > 1.0f) {
						continue;
					}
				}

				uint32_t src_px = 0;
				if (tex) {
					// Nearest sample.
					int sx = (int)(ru * (float)(tex_w - 1) + 0.5f);
					int sy = (int)(rv * (float)(tex_h - 1) + 0.5f);
					src_px = tex->pixels[sy * tex_w + sx];
					// Treat magenta as transparent for consistency with sprites.
					if ((src_px & 0x00FFFFFFu) == 0x00FF00FFu) {
						continue;
					}
					// Apply tint blend if requested.
					float blend = clampf2(k.color_blend_opacity, 0.0f, 1.0f);
					if (blend > 0.0f) {
						uint8_t sa = (src_px >> 24u) & 0xFFu;
						uint8_t sb = (src_px >> 16u) & 0xFFu;
						uint8_t sg = (src_px >> 8u) & 0xFFu;
						uint8_t sr0 = src_px & 0xFFu;
						uint8_t nr = (uint8_t)lroundf(lerpf((float)sr0, (float)tint_r, blend));
						uint8_t ng = (uint8_t)lroundf(lerpf((float)sg, (float)tint_g, blend));
						uint8_t nb = (uint8_t)lroundf(lerpf((float)sb, (float)tint_b, blend));
						src_px = pack_abgr_u8(sa, nb, ng, nr);
					}
					// Apply particle opacity.
					src_px = mul_alpha_u8(src_px, out_a);
					if (((src_px >> 24u) & 0xFFu) == 0u) {
						continue;
					}
				} else {
					if (p->shape == PARTICLE_SHAPE_CIRCLE) {
						float rr = lx * lx + ly * ly;
						if (rr > 0.25f) {
							continue;
						}
					}
					// Shape particle: ignore color_blend_opacity, always full tint.
					src_px = pack_abgr_u8(out_a, tint_b, tint_g, tint_r);
				}

				uint32_t dst_px = fb->pixels[y * fb->width + x];
				fb->pixels[y * fb->width + x] = blend_abgr8888_over(src_px, dst_px);
			}
		}
	}
}
