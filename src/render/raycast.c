#include "render/raycast.h"

#include "render/draw.h"
#include "render/lighting.h"

#include "platform/time.h"

#include "game/world.h"

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#define MAX_VISIBLE_LIGHTS 96

// Caps the number of point lights considered per frame after view culling.
// Planes (floors/ceilings) do per-pixel lighting and dominate cost, so we use
// a smaller cap there than for walls.
#define MAX_ACTIVE_LIGHTS_WALLS 8
#define MAX_ACTIVE_LIGHTS_PLANES 6

static float deg_to_rad(float deg);

static bool g_point_lights_enabled = true;

void raycast_set_point_lights_enabled(bool enabled) {
	g_point_lights_enabled = enabled;
}

static uint32_t hash_u32(uint32_t x) {
	// SplitMix32
	x += 0x9E3779B9u;
	x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
	x = (x ^ (x >> 13)) * 0xC2B2AE35u;
	return x ^ (x >> 16);
}

static float rand01(uint32_t seed) {
	uint32_t x = hash_u32(seed);
	return (float)(x & 0x00FFFFFFu) / (float)0x01000000u;
}

static float smoothstep(float t) {
	if (t < 0.0f) {
		return 0.0f;
	}
	if (t > 1.0f) {
		return 1.0f;
	}
	return t * t * (3.0f - 2.0f * t);
}

static float flicker_factor_flame(uint32_t seed, float time_s) {
	// Smooth value-noise: ~8 Hz.
	const float freq = 8.0f;
	float t = time_s * freq;
	float ti = floorf(t);
	float tf = t - ti;
	uint32_t step = (uint32_t)ti;
	float a = rand01(seed ^ (step * 0xA511E9B3u));
	float b = rand01(seed ^ ((step + 1u) * 0xA511E9B3u));
	float n = a + (b - a) * smoothstep(tf);
	// Bias bright with occasional dips.
	return 0.65f + 0.45f * (n * n);
}

static float flicker_factor_malfunction(uint32_t seed, float time_s) {
	// Abrupt spurts: mostly on, occasional off/strobe.
	const float freq = 22.0f;
	float t = time_s * freq;
	float ti = floorf(t);
	float tf = t - ti;
	uint32_t step = (uint32_t)ti;
	float r = rand01(seed ^ (step * 0xD1B54A35u));
	if (r < 0.06f) {
		return 0.0f;
	}
	if (r < 0.10f) {
		float s = rand01(seed ^ (step * 0x94D049BBu));
		float hz = 8.0f + 24.0f * s;
		float p = sinf(tf * (float)M_PI * 2.0f * hz);
		return (p > 0.0f) ? 1.0f : 0.0f;
	}
	return 0.90f + 0.10f * rand01(seed ^ (step * 0x9E3779B9u));
}

static float light_score_for_camera(const PointLight* L, float cam_x, float cam_y) {
	if (!L || L->radius <= 0.0f || L->intensity <= 0.0f) {
		return 0.0f;
	}
	float dx = L->x - cam_x;
	float dy = L->y - cam_y;
	float d2 = dx * dx + dy * dy;
	float r = L->radius;
	float r2 = r * r;
	if (d2 >= r2) {
		// Still score it, but much lower (it can still affect some visible pixels).
		// Use a soft falloff based on distance.
		float d = sqrtf(d2);
		float t = 1.0f - (d / (r + 1e-3f));
		if (t < 0.0f) {
			t = 0.0f;
		}
		return L->intensity * (t * t) * 0.25f;
	}
	// Inside radius: treat as highly relevant.
	float d = sqrtf(d2);
	float t = 1.0f - (d / (r + 1e-3f));
	return 1000.0f + L->intensity * (t * t);
}

static int limit_visible_lights(
	PointLight* lights,
	int count,
	int max_keep,
	float cam_x,
	float cam_y
) {
	if (!lights || count <= 0) {
		return 0;
	}
	if (max_keep <= 0) {
		return 0;
	}
	if (count <= max_keep) {
		return count;
	}

	// Keep the top-N by score using a small insertion scheme (N is tiny).
	PointLight best[MAX_ACTIVE_LIGHTS_WALLS];
	float best_score[MAX_ACTIVE_LIGHTS_WALLS];
	int n = 0;

	int cap = max_keep;
	if (cap > MAX_ACTIVE_LIGHTS_WALLS) {
		cap = MAX_ACTIVE_LIGHTS_WALLS;
	}

	for (int i = 0; i < cap; i++) {
		best_score[i] = -1.0f;
	}

	for (int i = 0; i < count; i++) {
		float s = light_score_for_camera(&lights[i], cam_x, cam_y);
		// Find insertion position into ascending list.
		int insert = -1;
		if (n < cap) {
			insert = n;
			n++;
		} else if (s > best_score[0]) {
			insert = 0;
		} else {
			continue;
		}

		best[insert] = lights[i];
		best_score[insert] = s;
		// Bubble up to keep smallest at index 0.
		for (int j = insert; j > 0; j--) {
			if (best_score[j] < best_score[j - 1]) {
				float ts = best_score[j];
				best_score[j] = best_score[j - 1];
				best_score[j - 1] = ts;
				PointLight tl = best[j];
				best[j] = best[j - 1];
				best[j - 1] = tl;
			}
		}
	}

	// Write back best lights (order doesn't matter for lighting accumulation).
	for (int i = 0; i < n; i++) {
		lights[i] = best[i];
	}
	return n;
}

static int build_visible_lights_uncapped(
	PointLight* out,
	int out_cap,
	const World* world,
	const Camera* cam,
	float time_s
) {
	if (!out || out_cap <= 0 || !world || !cam || !world->lights || world->light_count <= 0) {
		return 0;
	}

	float cam_rad = deg_to_rad(cam->angle_deg);
	float fwdx = cosf(cam_rad);
	float fwdy = sinf(cam_rad);
	float fov_half = 0.5f * deg_to_rad(cam->fov_deg);
	float margin = deg_to_rad(12.0f);
	float cos_min = cosf(fov_half + margin);

	int n = 0;
	for (int i = 0; i < world->light_count && n < out_cap; i++) {
		if (world->light_alive && !world->light_alive[i]) {
			continue;
		}
		const PointLight* L = &world->lights[i];
		if (L->radius <= 0.0f || L->intensity <= 0.0f) {
			continue;
		}
		float vx = L->x - cam->x;
		float vy = L->y - cam->y;
		float d2 = vx * vx + vy * vy;
		float max_dist = 32.0f + L->radius;
		if (d2 > max_dist * max_dist) {
			continue;
		}
		float d = sqrtf(d2);
		float cos_ang = 1.0f;
		if (d > 1e-5f) {
			cos_ang = (vx * fwdx + vy * fwdy) / d;
		}
		if (!(cos_ang >= cos_min || d <= L->radius)) {
			continue;
		}

		PointLight tmp = *L;
		uint32_t seed = tmp.seed ? tmp.seed : (uint32_t)i;
		float f = 1.0f;
		switch (tmp.flicker) {
			case LIGHT_FLICKER_NONE:
				f = 1.0f;
				break;
			case LIGHT_FLICKER_FLAME:
				f = flicker_factor_flame(seed, time_s);
				break;
			case LIGHT_FLICKER_MALFUNCTION:
				f = flicker_factor_malfunction(seed, time_s);
				break;
			default:
				f = 1.0f;
				break;
		}
		if (f < 0.0f) {
			f = 0.0f;
		}
		tmp.intensity *= f;
		out[n++] = tmp;
	}
	return n;
}

static int build_visible_lights(
	PointLight* out,
	int out_cap,
	const World* world,
	const Camera* cam,
	float time_s
) {
	int n = build_visible_lights_uncapped(out, out_cap, world, cam, time_s);
	// Cap after view culling to keep per-pixel lighting work bounded.
	return limit_visible_lights(out, n, MAX_ACTIVE_LIGHTS_WALLS, cam->x, cam->y);
}

static void raycast_perf_reset(RaycastPerf* p) {
	if (!p) {
		return;
	}
	memset(p, 0, sizeof(*p));
}

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

static uint8_t clamp_u8(int v) {
	if (v < 0) {
		return 0;
	}
	if (v > 255) {
		return 255;
	}
	return (uint8_t)v;
}

static uint32_t apply_lighting_mul_u8(uint32_t rgba, int r_mul_i, int g_mul_i, int b_mul_i) {
	uint8_t a = (uint8_t)((rgba >> 24) & 0xFF);
	uint8_t r = (uint8_t)((rgba >> 16) & 0xFF);
	uint8_t g = (uint8_t)((rgba >> 8) & 0xFF);
	uint8_t b = (uint8_t)(rgba & 0xFF);
	int rr = (r * r_mul_i + 128) >> 8;
	int gg = (g * g_mul_i + 128) >> 8;
	int bb = (b * b_mul_i + 128) >> 8;
	return ((uint32_t)a << 24) | ((uint32_t)clamp_u8(rr) << 16) | ((uint32_t)clamp_u8(gg) << 8) | (uint32_t)clamp_u8(bb);
}

static float fractf(float v) {
	return v - floorf(v);
}

// Renderer helper: stable sector lookup with internal memory.
// Uses shared world_find_sector_at_point_stable from game/world.c.
static int raycast_find_sector_at_point_stable(const World* world, float px, float py) {
	static int s_last_valid_sector = -1;
	if (!world || world->sector_count <= 0) {
		s_last_valid_sector = -1;
		return -1;
	}
	if ((unsigned)s_last_valid_sector >= (unsigned)world->sector_count) {
		s_last_valid_sector = -1;
	}
	int s = world_find_sector_at_point(world, px, py);
	if ((unsigned)s < (unsigned)world->sector_count) {
		s_last_valid_sector = s;
		return s;
	}
	return s_last_valid_sector;
}

// Pick which sector is "on the camera side" of the wall.
// Assumption: wall's (v0->v1) winding defines the half-plane for front/back sectors.
// If winding is inconsistent, we still fall back to any valid sector index.
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

static float camera_z_for_sector(const World* world, int sector, float z_offset) {
	// World units: sector floor/ceil are in the same units as map vertices.
	// We currently render with a fixed eye height above the sector floor.
	// (Player has no z yet; this is purely for rendering.)
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

void raycast_render_untextured(Framebuffer* fb, const World* world, const Camera* cam) {
	// Background: sky + floor
	draw_clear(fb, 0xFF0B0E14u);
	draw_rect(fb, 0, fb->height / 2, fb->width, fb->height / 2, 0xFF121018u);

	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		return;
	}

	float angle0 = cam->angle_deg - cam->fov_deg * 0.5f;
	float inv_w = fb->width > 1 ? (1.0f / (float)(fb->width - 1)) : 0.0f;
	float half_h = 0.5f * (float)fb->height;
	float proj_z = half_h;
	int plane_sector = raycast_find_sector_at_point_stable(world, cam->x, cam->y);
	float cam_z = camera_z_for_sector(world, plane_sector, cam->z);

	PointLight vis_lights[MAX_VISIBLE_LIGHTS];
	int vis_count = 0;
	if (g_point_lights_enabled) {
		vis_count = build_visible_lights(vis_lights, MAX_VISIBLE_LIGHTS, world, cam, (float)platform_time_seconds());
	}

	for (int x = 0; x < fb->width; x++) {
		float lerp = (float)x * inv_w;
		float ray_deg = angle0 + lerp * cam->fov_deg;
		float ray_rad = deg_to_rad(ray_deg);
		float dx = cosf(ray_rad);
		float dy = sinf(ray_rad);

		float best_t = 1e30f;
		int best_wall = -1;
		for (int i = 0; i < world->wall_count; i++) {
			Wall w = world->walls[i];
			if (w.v0 < 0 || w.v0 >= world->vertex_count || w.v1 < 0 || w.v1 >= world->vertex_count) {
				continue;
			}
			Vertex a = world->vertices[w.v0];
			Vertex b = world->vertices[w.v1];
			float t = 0.0f;
			if (ray_segment_hit(cam->x, cam->y, dx, dy, a.x, a.y, b.x, b.y, &t)) {
				if (t < best_t) {
					best_t = t;
					best_wall = i;
				}
			}
		}

		if (best_wall < 0) {
			continue;
		}

		// Fisheye correction
		float cam_rad = deg_to_rad(cam->angle_deg);
		float corr = cosf(ray_rad - cam_rad);
		float dist = best_t * (corr > 0.001f ? corr : 0.001f);

		Wall w = world->walls[best_wall];
		uint32_t base = 0xFFB0B0B0u;
		float sector_intensity = 1.0f;
		LightColor sector_tint = light_color_white();
		int view_sector = wall_sector_for_point(world, &w, cam->x, cam->y);
		if ((unsigned)view_sector < (unsigned)world->sector_count) {
			const Sector* s = &world->sectors[view_sector];
			sector_intensity = s->light;
			sector_tint = s->light_color;
		}

		float wall_floor_z = 0.0f;
		float wall_ceil_z = 4.0f;
		if ((unsigned)view_sector < (unsigned)world->sector_count) {
			const Sector* s = &world->sectors[view_sector];
			wall_floor_z = s->floor_z;
			wall_ceil_z = s->ceil_z;
		}
		int y_top = (int)(half_h - (wall_ceil_z - cam_z) * (proj_z / (dist + 0.001f)));
		int y_bot = (int)(half_h - (wall_floor_z - cam_z) * (proj_z / (dist + 0.001f)));
		if (y_top < 0) {
			y_top = 0;
		}
		if (y_bot > fb->height) {
			y_bot = fb->height;
		}
		float hit_x = cam->x + dx * best_t;
		float hit_y = cam->y + dy * best_t;
		uint32_t c = lighting_apply(base, dist, sector_intensity, sector_tint, vis_lights, vis_count, hit_x, hit_y);

		for (int y = y_top; y < y_bot; y++) {
			fb->pixels[y * fb->width + x] = c;
		}
	}
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

static LightColor lightcolor_lerp(LightColor a, LightColor b, float t) {
	if (t < 0.0f) {
		t = 0.0f;
	} else if (t > 1.0f) {
		t = 1.0f;
	}
	LightColor out;
	out.r = a.r + (b.r - a.r) * t;
	out.g = a.g + (b.g - a.g) * t;
	out.b = a.b + (b.b - a.b) * t;
	return out;
}

static int find_nearest_wall_hit_in_sector(
	const World* world,
	int sector,
	float ox,
	float oy,
	float dx,
	float dy,
	float t_min,
	int ignore_wall_index,
	float* out_t,
	RaycastPerf* perf
) {
	if (!world || (unsigned)sector >= (unsigned)world->sector_count || !out_t) {
		return -1;
	}
	float best_t = 1e30f;
	int best_wall = -1;
	if (world->sector_wall_offsets && world->sector_wall_counts && world->sector_wall_indices) {
		int start = world->sector_wall_offsets[sector];
		int count = world->sector_wall_counts[sector];
		for (int wi = 0; wi < count; wi++) {
			int i = world->sector_wall_indices[start + wi];
			if (i == ignore_wall_index) {
				continue;
			}
			if ((unsigned)i >= (unsigned)world->wall_count) {
				continue;
			}
			const Wall* w = &world->walls[i];
			if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
				continue;
			}
			Vertex a = world->vertices[w->v0];
			Vertex b = world->vertices[w->v1];
			float t = 0.0f;
			if (perf) {
				perf->wall_ray_tests++;
			}
			if (ray_segment_hit(ox, oy, dx, dy, a.x, a.y, b.x, b.y, &t)) {
				if (t > t_min && t < best_t) {
					best_t = t;
					best_wall = i;
				}
			}
		}
	} else {
		for (int i = 0; i < world->wall_count; i++) {
			if (i == ignore_wall_index) {
				continue;
			}
			const Wall* w = &world->walls[i];
			// Double-sided solid walls: a "one-sided" wall should still render when viewed
			// from the back (e.g. if the player ends up in an adjacent sector or outside).
			if (w->back_sector != -1) {
				if (w->front_sector != sector && w->back_sector != sector) {
					continue;
				}
			}
			if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
				continue;
			}
			Vertex a = world->vertices[w->v0];
			Vertex b = world->vertices[w->v1];
			float t = 0.0f;
			if (perf) {
				perf->wall_ray_tests++;
			}
			if (ray_segment_hit(ox, oy, dx, dy, a.x, a.y, b.x, b.y, &t)) {
				if (t > t_min && t < best_t) {
					best_t = t;
					best_wall = i;
				}
			}
		}
	}
	if (best_wall >= 0) {
		*out_t = best_t;
	}
	return best_wall;
}

static int project_y(float half_h, float proj_dist, float cam_z, float z, float dist) {
	return (int)(half_h - (z - cam_z) * (proj_dist / (dist + 0.001f)));
}

static bool is_sky_sentinel(const char* s) {
	return s && (strcmp(s, "SKY") == 0 || strcmp(s, "sky") == 0);
}

static inline void depth_pixels_write_min(float* depth_pixels, int w, int x, int y, float depth) {
	if (!depth_pixels || w <= 0 || x < 0 || y < 0) {
		return;
	}
	size_t idx = (size_t)y * (size_t)w + (size_t)x;
	if (depth_pixels[idx] > depth) {
		depth_pixels[idx] = depth;
	}
}

static void draw_sector_ceiling_column(
	Framebuffer* fb,
	float* out_depth_pixels,
	int x,
	int y_top,
	int y_bot,
	float half_h,
	float proj_dist,
	float cam_x,
	float cam_y,
	float cam_z,
	float dx,
	float dy,
	float corr,
	float ceil_z,
	const Texture* ceil_tex,
	const Texture* sky_tex,
	float sector_intensity,
	LightColor sector_tint,
	const PointLight* lights,
	int light_count,
	RaycastPerf* perf
) {
	if (!fb || x < 0 || x >= fb->width) {
		return;
	}
	if (y_top < 0) {
		y_top = 0;
	}
	if (y_bot > fb->height) {
		y_bot = fb->height;
	}
	if (y_top >= y_bot) {
		return;
	}
	if (perf) {
		perf->pixels_ceil += (uint32_t)(y_bot - y_top);
	}

	// Skybox: cylindrical mapping, no perspective floor-plane math.
	if (sky_tex) {
		if (y_top < 0) {
			y_top = 0;
		}
		if (y_bot > fb->height) {
			y_bot = fb->height;
		}
		float ang = atan2f(dy, dx);
		float u = (ang + (float)M_PI) / (2.0f * (float)M_PI);
		// y in [0, half_h] -> v in [0, 1]
		float inv_half = half_h > 1e-6f ? (1.0f / half_h) : 1.0f;
		for (int y = y_top; y < y_bot; y++) {
			float v = ((float)y + 0.5f) * inv_half;
			v = clampf(v, 0.0f, 1.0f);
			fb->pixels[y * fb->width + x] = texture_sample_nearest(sky_tex, u, v);
		}
		return;
	}

	float corr_safe = corr > 0.001f ? corr : 0.001f;
	int y_horizon = (int)half_h;

	// Ceiling
	if (ceil_z > cam_z + 0.001f) {
		// Plane texture mapping: previously used fractf(wx/wy) which repeats every 1 world unit
		// and makes floor/ceiling textures look extremely small. Scale UVs so one tile spans
		// multiple world units (closer to typical wall texel density).
		const float plane_uv_scale = 0.25f; // 1 repeat per 4 world units
		int cy0 = y_top;
		int cy1 = y_bot < y_horizon ? y_bot : y_horizon;
		const int light_step = 4;
		int r_mul_i = 256;
		int g_mul_i = 256;
		int b_mul_i = 256;
		for (int y = cy0; y < cy1; y++) {
			float denom = half_h - (float)y;
			if (denom <= 0.001f) {
				continue;
			}
			float row_dist = ((ceil_z - cam_z) * proj_dist) / denom;
			depth_pixels_write_min(out_depth_pixels, fb->width, x, y, row_dist);
			float t = row_dist / corr_safe;
			float wx = cam_x + dx * t;
			float wy = cam_y + dy * t;
			float tu = fractf(wx * plane_uv_scale);
			float tv = fractf(wy * plane_uv_scale);
			uint32_t c = ceil_tex ? texture_sample_nearest(ceil_tex, tu, tv) : 0xFF0B0E14u;
			if (lights && light_count > 0) {
				if (((y - cy0) % light_step) == 0) {
					LightColor mul = lighting_compute_multipliers(row_dist, sector_intensity, sector_tint, lights, light_count, wx, wy);
					float r_mul = lighting_quantize_factor(mul.r);
					float g_mul = lighting_quantize_factor(mul.g);
					float b_mul = lighting_quantize_factor(mul.b);
					r_mul_i = (int)lroundf(clampf(r_mul, 0.0f, 1.0f) * 256.0f);
					g_mul_i = (int)lroundf(clampf(g_mul, 0.0f, 1.0f) * 256.0f);
					b_mul_i = (int)lroundf(clampf(b_mul, 0.0f, 1.0f) * 256.0f);
					if (r_mul_i < 0) {
						r_mul_i = 0;
					}
					if (g_mul_i < 0) {
						g_mul_i = 0;
					}
					if (b_mul_i < 0) {
						b_mul_i = 0;
					}
					if (r_mul_i > 256) {
						r_mul_i = 256;
					}
					if (g_mul_i > 256) {
						g_mul_i = 256;
					}
					if (b_mul_i > 256) {
						b_mul_i = 256;
					}
					if (perf) {
						perf->lighting_apply_calls++;
						perf->lighting_apply_light_iters += (uint64_t)light_count;
					}
				}
				c = apply_lighting_mul_u8(c, r_mul_i, g_mul_i, b_mul_i);
			} else {
				if (perf) {
					perf->lighting_apply_calls++;
					perf->lighting_apply_light_iters += 0;
				}
				c = lighting_apply(c, row_dist, sector_intensity, sector_tint, NULL, 0, wx, wy);
			}
			fb->pixels[y * fb->width + x] = c;
		}
	}
}

static void draw_sector_floor_column(
	Framebuffer* fb,
	float* out_depth_pixels,
	int x,
	int y_top,
	int y_bot,
	float half_h,
	float proj_dist,
	float cam_x,
	float cam_y,
	float cam_z,
	float dx,
	float dy,
	float corr,
	float floor_z,
	const Texture* floor_tex,
	float sector_intensity,
	LightColor sector_tint,
	const PointLight* lights,
	int light_count,
	RaycastPerf* perf
) {
	if (!fb || x < 0 || x >= fb->width) {
		return;
	}
	if (y_top < 0) {
		y_top = 0;
	}
	if (y_bot > fb->height) {
		y_bot = fb->height;
	}
	if (y_top >= y_bot) {
		return;
	}
	if (perf) {
		perf->pixels_floor += (uint32_t)(y_bot - y_top);
	}

	float corr_safe = corr > 0.001f ? corr : 0.001f;
	int y_horizon = (int)half_h;

	// Floor
	if (cam_z > floor_z + 0.001f) {
		const float plane_uv_scale = 0.25f; // 1 repeat per 4 world units
		int fy0 = y_top > y_horizon ? y_top : y_horizon;
		int fy1 = y_bot;
		const int light_step = 4;
		int r_mul_i = 256;
		int g_mul_i = 256;
		int b_mul_i = 256;
		for (int y = fy0; y < fy1; y++) {
			float denom = (float)y - half_h;
			if (denom <= 0.001f) {
				continue;
			}
			float row_dist = ((cam_z - floor_z) * proj_dist) / denom;
			depth_pixels_write_min(out_depth_pixels, fb->width, x, y, row_dist);
			float t = row_dist / corr_safe;
			float wx = cam_x + dx * t;
			float wy = cam_y + dy * t;
			float tu = fractf(wx * plane_uv_scale);
			float tv = fractf(wy * plane_uv_scale);
			uint32_t c = floor_tex ? texture_sample_nearest(floor_tex, tu, tv) : 0xFF121018u;
			if (lights && light_count > 0) {
				if (((y - fy0) % light_step) == 0) {
					LightColor mul = lighting_compute_multipliers(row_dist, sector_intensity, sector_tint, lights, light_count, wx, wy);
					float r_mul = lighting_quantize_factor(mul.r);
					float g_mul = lighting_quantize_factor(mul.g);
					float b_mul = lighting_quantize_factor(mul.b);
					r_mul_i = (int)lroundf(clampf(r_mul, 0.0f, 1.0f) * 256.0f);
					g_mul_i = (int)lroundf(clampf(g_mul, 0.0f, 1.0f) * 256.0f);
					b_mul_i = (int)lroundf(clampf(b_mul, 0.0f, 1.0f) * 256.0f);
					if (r_mul_i < 0) {
						r_mul_i = 0;
					}
					if (g_mul_i < 0) {
						g_mul_i = 0;
					}
					if (b_mul_i < 0) {
						b_mul_i = 0;
					}
					if (r_mul_i > 256) {
						r_mul_i = 256;
					}
					if (g_mul_i > 256) {
						g_mul_i = 256;
					}
					if (b_mul_i > 256) {
						b_mul_i = 256;
					}
					if (perf) {
						perf->lighting_apply_calls++;
						perf->lighting_apply_light_iters += (uint64_t)light_count;
					}
				}
				c = apply_lighting_mul_u8(c, r_mul_i, g_mul_i, b_mul_i);
			} else {
				if (perf) {
					perf->lighting_apply_calls++;
					perf->lighting_apply_light_iters += 0;
				}
				c = lighting_apply(c, row_dist, sector_intensity, sector_tint, NULL, 0, wx, wy);
			}
			fb->pixels[y * fb->width + x] = c;
		}
	}
}

static void render_wall_span_textured(
	Framebuffer* fb,
	float* out_depth_pixels,
	int x,
	int y_top,
	int y_bot,
	int y_clip_top,
	int y_clip_bot,
	float z_top,
	float z_bot,
	float half_h,
	float proj_dist,
	float cam_z,
	float u_lerp,
	float u_tex,
	const Texture* tex,
	uint32_t base,
	float dist,
	LightColor wall_mul_v0,
	LightColor wall_mul_v1,
	RaycastPerf* perf
) {
	(void)z_top;
	(void)z_bot;
	if (!fb || x < 0 || x >= fb->width) {
		return;
	}
	if (y_top < y_clip_top) {
		y_top = y_clip_top;
	}
	if (y_bot > y_clip_bot) {
		y_bot = y_clip_bot;
	}
	if (y_top < 0) {
		y_top = 0;
	}
	if (y_bot > fb->height) {
		y_bot = fb->height;
	}
	if (y_top >= y_bot) {
		return;
	}
	if (perf) {
		perf->pixels_wall += (uint32_t)(y_bot - y_top);
	}
	float inv_proj = proj_dist > 1e-6f ? (1.0f / proj_dist) : 1.0f;

	// PS1/N64-style Gouraud wall lighting: interpolate endpoint multipliers by wall U,
	// then quantize to produce visible banding.
	LightColor mul = lightcolor_lerp(wall_mul_v0, wall_mul_v1, u_lerp);
	float r_mul = lighting_quantize_factor(mul.r);
	float g_mul = lighting_quantize_factor(mul.g);
	float b_mul = lighting_quantize_factor(mul.b);
	int r_mul_i = (int)lroundf(clampf(r_mul, 0.0f, 1.0f) * 256.0f);
	int g_mul_i = (int)lroundf(clampf(g_mul, 0.0f, 1.0f) * 256.0f);
	int b_mul_i = (int)lroundf(clampf(b_mul, 0.0f, 1.0f) * 256.0f);
	if (r_mul_i < 0) {
		r_mul_i = 0;
	}
	if (g_mul_i < 0) {
		g_mul_i = 0;
	}
	if (b_mul_i < 0) {
		b_mul_i = 0;
	}
	if (r_mul_i > 256) {
		r_mul_i = 256;
	}
	if (g_mul_i > 256) {
		g_mul_i = 256;
	}
	if (b_mul_i > 256) {
		b_mul_i = 256;
	}

	// Wall texture mapping: tile in world-space; do not scale with wall height.
	// We derive world-space Z at each screen pixel and wrap it.
	const float wall_uv_scale_u = 0.25f; // 1 repeat per 4 world units
	const float wall_uv_scale_v = 0.25f; // 1 repeat per 4 world units
	float uu = fractf(u_tex * wall_uv_scale_u);
	float yf0 = (float)y_top + 0.5f;
	float z0 = cam_z + (half_h - yf0) * dist * inv_proj;
	float dz = -dist * inv_proj;

	for (int y = y_top; y < y_bot; y++) {
		depth_pixels_write_min(out_depth_pixels, fb->width, x, y, dist);
		float vv = fractf(z0 * wall_uv_scale_v);
		uint32_t c = tex ? texture_sample_nearest(tex, uu, vv) : base;
		uint8_t a = (uint8_t)((c >> 24) & 0xFF);
		uint8_t r = (uint8_t)((c >> 16) & 0xFF);
		uint8_t g = (uint8_t)((c >> 8) & 0xFF);
		uint8_t b = (uint8_t)(c & 0xFF);
		int rr = (r * r_mul_i + 128) >> 8;
		int gg = (g * g_mul_i + 128) >> 8;
		int bb = (b * b_mul_i + 128) >> 8;
		fb->pixels[y * fb->width + x] = ((uint32_t)a << 24) | ((uint32_t)clamp_u8(rr) << 16) | ((uint32_t)clamp_u8(gg) << 8) | (uint32_t)clamp_u8(bb);
		z0 += dz;
	}
}

static void draw_sector_planes_column(
	Framebuffer* fb,
	float* out_depth_pixels,
	int x,
	int y_top,
	int y_bot,
	float half_h,
	float proj_dist,
	float cam_x,
	float cam_y,
	float cam_z,
	float dx,
	float dy,
	float corr,
	float floor_z,
	float ceil_z,
	const Texture* floor_tex,
	const Texture* ceil_tex,
	const Texture* sky_tex,
	float sector_intensity,
	LightColor sector_tint,
	const PointLight* lights,
	int light_count,
	RaycastPerf* perf
) {
	draw_sector_ceiling_column(
		fb,
		out_depth_pixels,
		x,
		y_top,
		y_bot,
		half_h,
		proj_dist,
		cam_x,
		cam_y,
		cam_z,
		dx,
		dy,
		corr,
		ceil_z,
		ceil_tex,
		sky_tex,
		sector_intensity,
		sector_tint,
		lights,
		light_count,
		perf
	);
	draw_sector_floor_column(
		fb,
		out_depth_pixels,
		x,
		y_top,
		y_bot,
		half_h,
		proj_dist,
		cam_x,
		cam_y,
		cam_z,
		dx,
		dy,
		corr,
		floor_z,
		floor_tex,
		sector_intensity,
		sector_tint,
		lights,
		light_count,
		perf
	);
}

static void render_column_textured_recursive(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const Texture* sky_tex,
	const PointLight* plane_lights,
	int plane_light_count,
	const PointLight* wall_lights,
	int wall_light_count,
	int x,
	float half_h,
	float proj_dist,
	float cam_z,
	float ray_dx,
	float ray_dy,
	float corr,
	int sector,
	int y_clip_top,
	int y_clip_bot,
	float t_min,
	int ignore_wall,
	int depth,
	float* out_depth,
	float* out_depth_pixels,
	RaycastPerf* perf
) {
	if (!fb || !world || !cam) {
		return;
	}
	if (depth > 8) {
		return;
	}
	if (perf) {
		perf->portal_calls++;
		if ((uint32_t)depth > perf->portal_max_depth) {
			perf->portal_max_depth = (uint32_t)depth;
		}
	}
	if ((unsigned)sector >= (unsigned)world->sector_count) {
		return;
	}
	if (y_clip_top < 0) {
		y_clip_top = 0;
	}
	if (y_clip_bot > fb->height) {
		y_clip_bot = fb->height;
	}
	if (y_clip_top >= y_clip_bot) {
		return;
	}

	const Sector* s = &world->sectors[sector];
	float sector_intensity = s->light;
	LightColor sector_tint = s->light_color;
	const Texture* floor_tex = NULL;
	const Texture* ceil_tex = NULL;
	bool ceil_is_sky = is_sky_sentinel(s->ceil_tex);
	if (texreg && paths) {
		if (s->floor_tex[0] != '\0') {
			floor_tex = texture_registry_get(texreg, paths, s->floor_tex);
		}
		if (s->ceil_tex[0] != '\0' && !ceil_is_sky) {
			ceil_tex = texture_registry_get(texreg, paths, s->ceil_tex);
		}
	}

	float hit_t = 0.0f;
	double hit_t0 = 0.0;
	if (perf) {
		hit_t0 = platform_time_seconds();
	}
	int hit_wall = find_nearest_wall_hit_in_sector(world, sector, cam->x, cam->y, ray_dx, ray_dy, t_min, ignore_wall, &hit_t, perf);
	if (perf) {
		perf->hit_test_ms += (platform_time_seconds() - hit_t0) * 1000.0;
	}
	if (hit_wall < 0) {
		double planes_t0 = 0.0;
		if (perf) {
			planes_t0 = platform_time_seconds();
		}
		draw_sector_planes_column(
			fb,
			out_depth_pixels,
			x,
			y_clip_top,
			y_clip_bot,
			half_h,
			proj_dist,
			cam->x,
			cam->y,
			cam_z,
			ray_dx,
			ray_dy,
			corr,
			s->floor_z,
			s->ceil_z,
			floor_tex,
			ceil_tex,
			(ceil_is_sky ? sky_tex : NULL),
			sector_intensity,
			sector_tint,
			plane_lights,
			plane_light_count,
			perf
		);
		if (perf) {
			perf->planes_ms += (platform_time_seconds() - planes_t0) * 1000.0;
		}
		return;
	}

	const Wall* w = &world->walls[hit_wall];
	float corr_safe = corr > 0.001f ? corr : 0.001f;
	float dist = hit_t * corr_safe;

	// NOTE: out_depth is used for sprite occlusion (see entity_system_draw_sprites).
	// We want portal boundaries to be transparent for occlusion when they have an
	// open span, otherwise entities in adjacent sectors get incorrectly culled.
	// So we only write depth for solid walls (and for "portals" with no open span).

	// Compute hit u along segment (0..1) for lighting interpolation, plus world-space
	// distance along the wall for texture tiling.
	Vertex a = world->vertices[w->v0];
	Vertex b = world->vertices[w->v1];
	float hit_x = cam->x + ray_dx * hit_t;
	float hit_y = cam->y + ray_dy * hit_t;
	float u = segment_u(a.x, a.y, b.x, b.y, hit_x, hit_y);
	float wall_len = hypotf(b.x - a.x, b.y - a.y);
	float u_tex = u * wall_len;

	// Per-vertex wall lighting multipliers (Gouraud). Compute once and reuse for spans.
	float da = hypotf(a.x - cam->x, a.y - cam->y);
	float db = hypotf(b.x - cam->x, b.y - cam->y);
	LightColor wall_mul_v0 = lighting_compute_multipliers(
		da,
		sector_intensity,
		sector_tint,
		wall_lights,
		wall_light_count,
		a.x,
		a.y
	);
	if (perf) {
		perf->lighting_mul_calls++;
		perf->lighting_mul_light_iters += (uint64_t)(wall_light_count > 0 ? wall_light_count : 0);
	}
	LightColor wall_mul_v1 = lighting_compute_multipliers(
		db,
		sector_intensity,
		sector_tint,
		wall_lights,
		wall_light_count,
		b.x,
		b.y
	);
	if (perf) {
		perf->lighting_mul_calls++;
		perf->lighting_mul_light_iters += (uint64_t)(wall_light_count > 0 ? wall_light_count : 0);
	}
	const Texture* wall_tex = NULL;
	if (texreg && paths) {
		wall_tex = texture_registry_get(texreg, paths, w->tex);
	}
	uint32_t base = 0xFFB0B0B0u;

	int other = -1;
	if (w->front_sector == sector) {
		other = w->back_sector;
	} else if (w->back_sector == sector) {
		other = w->front_sector;
	}

	// Solid wall
	if ((unsigned)other >= (unsigned)world->sector_count) {
		if (out_depth) {
			// Keep nearest depth for the column.
			if (out_depth[x] > dist) {
				out_depth[x] = dist;
			}
		}
		double walls_t0 = 0.0;
		if (perf) {
			walls_t0 = platform_time_seconds();
		}
		int y_top = project_y(half_h, proj_dist, cam_z, s->ceil_z, dist);
		int y_bot = project_y(half_h, proj_dist, cam_z, s->floor_z, dist);

		// Draw planes only outside the wall span (the wall will overwrite its pixels).
		int y_wall0 = y_top;
		int y_wall1 = y_bot;
		if (y_wall0 < y_clip_top) {
			y_wall0 = y_clip_top;
		}
		if (y_wall1 > y_clip_bot) {
			y_wall1 = y_clip_bot;
		}
		if (y_wall0 < 0) {
			y_wall0 = 0;
		}
		if (y_wall1 > fb->height) {
			y_wall1 = fb->height;
		}

		double planes_t0 = 0.0;
		if (perf) {
			planes_t0 = platform_time_seconds();
		}
		if (y_wall0 < y_wall1) {
			if (y_clip_top < y_wall0) {
				draw_sector_planes_column(
					fb,
							out_depth_pixels,
					x,
					y_clip_top,
					y_wall0,
					half_h,
					proj_dist,
					cam->x,
					cam->y,
					cam_z,
					ray_dx,
					ray_dy,
					corr,
					s->floor_z,
					s->ceil_z,
					floor_tex,
					ceil_tex,
					(ceil_is_sky ? sky_tex : NULL),
					sector_intensity,
					sector_tint,
					plane_lights,
					plane_light_count,
					perf
				);
			}
			if (y_wall1 < y_clip_bot) {
				draw_sector_planes_column(
					fb,
							out_depth_pixels,
					x,
					y_wall1,
					y_clip_bot,
					half_h,
					proj_dist,
					cam->x,
					cam->y,
					cam_z,
					ray_dx,
					ray_dy,
					corr,
					s->floor_z,
					s->ceil_z,
					floor_tex,
					ceil_tex,
					(ceil_is_sky ? sky_tex : NULL),
					sector_intensity,
					sector_tint,
					plane_lights,
					plane_light_count,
					perf
				);
			}
		} else {
			draw_sector_planes_column(
				fb,
			out_depth_pixels,
				x,
				y_clip_top,
				y_clip_bot,
				half_h,
				proj_dist,
				cam->x,
				cam->y,
				cam_z,
				ray_dx,
				ray_dy,
				corr,
				s->floor_z,
				s->ceil_z,
				floor_tex,
				ceil_tex,
				(ceil_is_sky ? sky_tex : NULL),
				sector_intensity,
				sector_tint,
				plane_lights,
				plane_light_count,
				perf
			);
		}
		if (perf) {
			perf->planes_ms += (platform_time_seconds() - planes_t0) * 1000.0;
		}

		render_wall_span_textured(
			fb,
			out_depth_pixels,
			x,
			y_top,
			y_bot,
			y_clip_top,
			y_clip_bot,
			s->ceil_z,
			s->floor_z,
			half_h,
			proj_dist,
			cam_z,
			u,
			u_tex,
			wall_tex,
			base,
			dist,
			wall_mul_v0,
			wall_mul_v1,
			perf
		);
		if (perf) {
			perf->walls_ms += (platform_time_seconds() - walls_t0) * 1000.0;
		}
		return;
	}

	// Portal wall: draw upper/lower pieces relative to this sector, then recurse through open span.
	const Sector* so = &world->sectors[other];
	bool other_ceil_is_sky = is_sky_sentinel(so->ceil_tex);

	float z_open_top = s->ceil_z < so->ceil_z ? s->ceil_z : so->ceil_z;
	float z_open_bot = s->floor_z > so->floor_z ? s->floor_z : so->floor_z;
	int y_open0 = 0;
	int y_open1 = 0;
	bool has_open = false;
	if (z_open_top > z_open_bot + 1e-4f) {
		int y_open_top = project_y(half_h, proj_dist, cam_z, z_open_top, dist);
		int y_open_bot = project_y(half_h, proj_dist, cam_z, z_open_bot, dist);
		y_open0 = y_open_top;
		y_open1 = y_open_bot;
		if (y_open0 < y_clip_top) {
			y_open0 = y_clip_top;
		}
		if (y_open1 > y_clip_bot) {
			y_open1 = y_clip_bot;
		}
		if (y_open0 < 0) {
			y_open0 = 0;
		}
		if (y_open1 > fb->height) {
			y_open1 = fb->height;
		}
		has_open = (y_open0 < y_open1);
	}
	if (!has_open) {
		// Treat as an occluder if the portal has no visible open span.
		if (out_depth) {
			if (out_depth[x] > dist) {
				out_depth[x] = dist;
			}
		}
	}

	// Recurse through the open span first, then draw this sector's planes only outside
	// that span. This avoids heavy portal-induced plane overdraw.
	if (has_open) {
		render_column_textured_recursive(
			fb,
			world,
			cam,
			texreg,
			paths,
			sky_tex,
			plane_lights,
			plane_light_count,
			wall_lights,
			wall_light_count,
			x,
			half_h,
			proj_dist,
			cam_z,
			ray_dx,
			ray_dy,
			corr,
			other,
			y_open0,
			y_open1,
			hit_t + 1e-4f,
			hit_wall,
			depth + 1,
			out_depth,
			out_depth_pixels,
			perf
		);
	}

	double planes_t0 = 0.0;
	if (perf) {
		planes_t0 = platform_time_seconds();
	}
	if (has_open) {
		if (y_clip_top < y_open0) {
			draw_sector_planes_column(
				fb,
				out_depth_pixels,
				x,
				y_clip_top,
				y_open0,
				half_h,
				proj_dist,
				cam->x,
				cam->y,
				cam_z,
				ray_dx,
				ray_dy,
				corr,
				s->floor_z,
				s->ceil_z,
				floor_tex,
				ceil_tex,
				(ceil_is_sky ? sky_tex : NULL),
				sector_intensity,
				sector_tint,
				plane_lights,
				plane_light_count,
				perf
			);
		}
		if (y_open1 < y_clip_bot) {
			draw_sector_planes_column(
				fb,
				out_depth_pixels,
				x,
				y_open1,
				y_clip_bot,
				half_h,
				proj_dist,
				cam->x,
				cam->y,
				cam_z,
				ray_dx,
				ray_dy,
				corr,
				s->floor_z,
				s->ceil_z,
				floor_tex,
				ceil_tex,
				(ceil_is_sky ? sky_tex : NULL),
				sector_intensity,
				sector_tint,
				plane_lights,
				plane_light_count,
				perf
			);
		}
	} else {
		draw_sector_planes_column(
			fb,
			out_depth_pixels,
			x,
			y_clip_top,
			y_clip_bot,
			half_h,
			proj_dist,
			cam->x,
			cam->y,
			cam_z,
			ray_dx,
			ray_dy,
			corr,
			s->floor_z,
			s->ceil_z,
			floor_tex,
			ceil_tex,
			(ceil_is_sky ? sky_tex : NULL),
			sector_intensity,
			sector_tint,
			plane_lights,
			plane_light_count,
			perf
		);
	}
	if (perf) {
		perf->planes_ms += (platform_time_seconds() - planes_t0) * 1000.0;
	}

	// Upper piece (if other ceiling is lower). If both ceilings are sky, don't draw.
	if (!((ceil_is_sky && other_ceil_is_sky)) && so->ceil_z < s->ceil_z - 1e-4f) {
		double walls_t0 = 0.0;
		if (perf) {
			walls_t0 = platform_time_seconds();
		}
		int y_top = project_y(half_h, proj_dist, cam_z, s->ceil_z, dist);
		int y_bot = project_y(half_h, proj_dist, cam_z, so->ceil_z, dist);
		render_wall_span_textured(
			fb,
			out_depth_pixels,
			x,
			y_top,
			y_bot,
			y_clip_top,
			y_clip_bot,
			s->ceil_z,
			so->ceil_z,
			half_h,
			proj_dist,
			cam_z,
			u,
			u_tex,
			wall_tex,
			base,
			dist,
			wall_mul_v0,
			wall_mul_v1,
			perf
		);
		if (perf) {
			perf->walls_ms += (platform_time_seconds() - walls_t0) * 1000.0;
		}
	}

	// Lower piece (if other floor is higher)
	if (so->floor_z > s->floor_z + 1e-4f) {
		double walls_t0 = 0.0;
		if (perf) {
			walls_t0 = platform_time_seconds();
		}
		int y_top = project_y(half_h, proj_dist, cam_z, so->floor_z, dist);
		int y_bot = project_y(half_h, proj_dist, cam_z, s->floor_z, dist);
		render_wall_span_textured(
			fb,
			out_depth_pixels,
			x,
			y_top,
			y_bot,
			y_clip_top,
			y_clip_bot,
			so->floor_z,
			s->floor_z,
			half_h,
			proj_dist,
			cam_z,
			u,
			u_tex,
			wall_tex,
			base,
			dist,
			wall_mul_v0,
			wall_mul_v1,
			perf
		);
		if (perf) {
			perf->walls_ms += (platform_time_seconds() - walls_t0) * 1000.0;
		}
	}

	(void)other_ceil_is_sky;
}


static void raycast_render_textured_from_sector_internal(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	float* out_depth_pixels,
	int start_sector,
	RaycastPerf* out_perf
) {
	TextureRegistryPerf texperf;
	if (out_perf) {
		raycast_perf_reset(out_perf);
		texture_registry_perf_begin(&texperf);
	}

	// Background: sky (floor/ceiling are drawn per column based on ray hit sector)
	draw_clear(fb, 0xFF0B0E14u);
	if (out_depth_pixels && fb && fb->width > 0 && fb->height > 0) {
		size_t n = (size_t)fb->width * (size_t)fb->height;
		for (size_t i = 0; i < n; i++) {
			out_depth_pixels[i] = 1e30f;
		}
	}

	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		if (out_depth) {
			for (int x = 0; x < fb->width; x++) {
				out_depth[x] = 1e30f;
			}
		}
		if (out_perf) {
			out_perf->tex_lookup_ms = texperf.get_ms;
			out_perf->texture_get_calls = texperf.get_calls;
			out_perf->registry_string_compares = texperf.registry_string_compares;
			texture_registry_perf_end();
		}
		return;
	}

	float angle0 = cam->angle_deg - cam->fov_deg * 0.5f;
	float inv_w = fb->width > 1 ? (1.0f / (float)(fb->width - 1)) : 0.0f;
	float half_h = 0.5f * (float)fb->height;
	float fov_rad = deg_to_rad(cam->fov_deg);
	float proj_dist = (0.5f * (float)fb->width) / tanf(0.5f * fov_rad);
	int start = start_sector;
	if ((unsigned)start >= (unsigned)(world ? world->sector_count : 0)) {
		start = -1;
	}
	if (start < 0) {
		start = raycast_find_sector_at_point_stable(world, cam->x, cam->y);
	}
	float cam_z = camera_z_for_sector(world, start, cam->z);
	float cam_rad = deg_to_rad(cam->angle_deg);
	const Texture* sky_tex = NULL;
	if (texreg && paths && sky_filename && sky_filename[0] != '\0') {
		sky_tex = texture_registry_get(texreg, paths, sky_filename);
	}

	PointLight vis_lights_uncapped[MAX_VISIBLE_LIGHTS];
	PointLight vis_lights_walls[MAX_VISIBLE_LIGHTS];
	PointLight vis_lights_planes[MAX_VISIBLE_LIGHTS];
	const PointLight* wall_lights_ptr = NULL;
	const PointLight* plane_lights_ptr = NULL;
	double lcull_t0 = 0.0;
	if (out_perf) {
		out_perf->lights_in_world = g_point_lights_enabled ? (uint32_t)(world->lights ? world->light_count : 0) : 0u;
		lcull_t0 = platform_time_seconds();
	}
	int vis_uncapped = 0;
	int vis_walls = 0;
	int vis_planes = 0;
	if (g_point_lights_enabled) {
		float t = (float)platform_time_seconds();
		vis_uncapped = build_visible_lights_uncapped(vis_lights_uncapped, MAX_VISIBLE_LIGHTS, world, cam, t);
		memcpy(vis_lights_walls, vis_lights_uncapped, (size_t)vis_uncapped * sizeof(PointLight));
		memcpy(vis_lights_planes, vis_lights_uncapped, (size_t)vis_uncapped * sizeof(PointLight));
		vis_walls = limit_visible_lights(vis_lights_walls, vis_uncapped, MAX_ACTIVE_LIGHTS_WALLS, cam->x, cam->y);
		vis_planes = limit_visible_lights(vis_lights_planes, vis_uncapped, MAX_ACTIVE_LIGHTS_PLANES, cam->x, cam->y);
		wall_lights_ptr = vis_lights_walls;
		plane_lights_ptr = vis_lights_planes;
	}
	if (out_perf) {
		out_perf->light_cull_ms += (platform_time_seconds() - lcull_t0) * 1000.0;
		out_perf->lights_visible_uncapped = (uint32_t)vis_uncapped;
		out_perf->lights_visible_walls = (uint32_t)vis_walls;
		out_perf->lights_visible_planes = (uint32_t)vis_planes;
	}

	for (int x = 0; x < fb->width; x++) {
		if (out_depth) {
			out_depth[x] = 1e30f;
		}
		float lerp = (float)x * inv_w;
		float ray_deg = angle0 + lerp * cam->fov_deg;
		float ray_rad = deg_to_rad(ray_deg);
		float dx = cosf(ray_rad);
		float dy = sinf(ray_rad);
		float corr = cosf(ray_rad - cam_rad);

		render_column_textured_recursive(
			fb,
			world,
			cam,
			texreg,
			paths,
			sky_tex,
			plane_lights_ptr,
			vis_planes,
			wall_lights_ptr,
			vis_walls,
			x,
			half_h,
			proj_dist,
			cam_z,
			dx,
			dy,
			corr,
			start,
			0,
			fb->height,
			0.0f,
			-1,
			0,
			out_depth,
			out_depth_pixels,
			out_perf
		);
	}

	if (out_perf) {
		out_perf->tex_lookup_ms = texperf.get_ms;
		out_perf->texture_get_calls = texperf.get_calls;
		out_perf->registry_string_compares = texperf.registry_string_compares;
		texture_registry_perf_end();
	}
}
void raycast_render_textured(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	float* out_depth_pixels
) {
	raycast_render_textured_from_sector(fb, world, cam, texreg, paths, sky_filename, out_depth, out_depth_pixels, -1);
}

void raycast_render_textured_from_sector(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	float* out_depth_pixels,
	int start_sector
) {
	raycast_render_textured_from_sector_internal(fb, world, cam, texreg, paths, sky_filename, out_depth, out_depth_pixels, start_sector, NULL);
}

void raycast_render_textured_from_sector_profiled(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	float* out_depth_pixels,
	int start_sector,
	RaycastPerf* out_perf
) {
	raycast_render_textured_from_sector_internal(fb, world, cam, texreg, paths, sky_filename, out_depth, out_depth_pixels, start_sector, out_perf);
}
