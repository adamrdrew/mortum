#include "render/raycast.h"

#include "render/draw.h"
#include "render/lighting.h"

#include <math.h>
#include <stdint.h>
#include <stdbool.h>

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

static float fractf(float v) {
	return v - floorf(v);
}

// Even-odd point-in-polygon test using all edges that touch a sector.
// Assumes walls form a closed boundary for each sector.
static bool sector_contains_point(const World* world, int sector, float px, float py) {
	if (!world || (unsigned)sector >= (unsigned)world->sector_count) {
		return false;
	}
	int crossings = 0;
	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (w->front_sector != sector && w->back_sector != sector) {
			continue;
		}
		if (w->v0 < 0 || w->v0 >= world->vertex_count || w->v1 < 0 || w->v1 >= world->vertex_count) {
			continue;
		}
		Vertex a = world->vertices[w->v0];
		Vertex b = world->vertices[w->v1];
		// Skip horizontal edges.
		if (fabsf(a.y - b.y) < 1e-8f) {
			continue;
		}
		bool cond = (a.y > py) != (b.y > py);
		if (!cond) {
			continue;
		}
		float x_int = (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x;
		if (px < x_int) {
			crossings ^= 1;
		}
	}
	return crossings != 0;
}

static int world_find_sector_at_point(const World* world, float px, float py) {
	if (!world || world->sector_count <= 0) {
		return -1;
	}
	for (int s = 0; s < world->sector_count; s++) {
		if (sector_contains_point(world, s, px, py)) {
			return s;
		}
	}
	return -1;
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

		float wall_height = 1.0f;
		int slice_h = (int)((wall_height * (float)fb->height) / (dist + 0.001f));
		if (slice_h > fb->height * 4) {
			slice_h = fb->height * 4;
		}
		int y0 = (fb->height - slice_h) / 2;
		int y1 = y0 + slice_h;
		if (y0 < 0) {
			y0 = 0;
		}
		if (y1 > fb->height) {
			y1 = fb->height;
		}

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
		float hit_x = cam->x + dx * best_t;
		float hit_y = cam->y + dy * best_t;
		uint32_t c = lighting_apply(base, dist, sector_intensity, sector_tint, world->lights, world->light_count, hit_x, hit_y);

		for (int y = y0; y < y1; y++) {
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

void raycast_render_textured(Framebuffer* fb, const World* world, const Camera* cam, TextureRegistry* texreg, const AssetPaths* paths, float* out_depth) {
	// Background: sky (floor/ceiling are drawn per column based on ray hit sector)
	draw_clear(fb, 0xFF0B0E14u);

	if (!world || world->wall_count <= 0 || world->vertex_count <= 0) {
		if (out_depth) {
			for (int x = 0; x < fb->width; x++) {
				out_depth[x] = 1e30f;
			}
		}
		return;
	}

	float angle0 = cam->angle_deg - cam->fov_deg * 0.5f;
	float inv_w = fb->width > 1 ? (1.0f / (float)(fb->width - 1)) : 0.0f;
	float half_h = 0.5f * (float)fb->height;
	float proj_z = half_h; // camera height in screen space (Wolf3D-style)
	int cam_sector = world_find_sector_at_point(world, cam->x, cam->y);

	for (int x = 0; x < fb->width; x++) {
		if (out_depth) {
			out_depth[x] = 1e30f;
		}
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
		if (out_depth) {
			out_depth[x] = dist;
		}

		float wall_height = 1.0f;
		int slice_h = (int)((wall_height * (float)fb->height) / (dist + 0.001f));
		if (slice_h > fb->height * 4) {
			slice_h = fb->height * 4;
		}
		int y0 = (fb->height - slice_h) / 2;
		int y1 = y0 + slice_h;
		int y0c = y0 < 0 ? 0 : y0;
		int y1c = y1 > fb->height ? fb->height : y1;
		if (y0c >= y1c) {
			continue;
		}

		Wall w = world->walls[best_wall];
		uint32_t base = 0xFFB0B0B0u;
		float wall_sector_intensity = 1.0f;
		LightColor wall_sector_tint = light_color_white();
		float plane_sector_intensity = 1.0f;
		LightColor plane_sector_tint = light_color_white();
		const Texture* floor_tex = NULL;
		const Texture* ceil_tex = NULL;
		int view_sector = wall_sector_for_point(world, &w, cam->x, cam->y);
		if ((unsigned)view_sector < (unsigned)world->sector_count) {
			const Sector* s = &world->sectors[view_sector];
			wall_sector_intensity = s->light;
			wall_sector_tint = s->light_color;
		}
		int plane_sector = cam_sector;
		if ((unsigned)plane_sector >= (unsigned)world->sector_count) {
			plane_sector = view_sector;
		}
		if ((unsigned)plane_sector < (unsigned)world->sector_count) {
			const Sector* s = &world->sectors[plane_sector];
			plane_sector_intensity = s->light;
			plane_sector_tint = s->light_color;
			if (texreg && paths) {
				if (s->floor_tex[0] != '\0') {
					floor_tex = texture_registry_get(texreg, paths, s->floor_tex);
				}
				if (s->ceil_tex[0] != '\0') {
					ceil_tex = texture_registry_get(texreg, paths, s->ceil_tex);
				}
			}
		}

		// Compute hit u along segment
		Vertex a = world->vertices[w.v0];
		Vertex b = world->vertices[w.v1];
		float hit_x = cam->x + dx * best_t;
		float hit_y = cam->y + dy * best_t;
		float u = segment_u(a.x, a.y, b.x, b.y, hit_x, hit_y);

		const Texture* tex = NULL;
		if (texreg && paths) {
			tex = texture_registry_get(texreg, paths, w.tex);
		}

		// Ceiling (textured)
		if (y0c > 0) {
			for (int y = 0; y < y0c; y++) {
				float denom = half_h - (float)y;
				if (denom <= 0.001f) {
					continue;
				}
				float row_dist = proj_z / denom;
				float t = row_dist / (corr > 0.001f ? corr : 0.001f);
				float wx = cam->x + dx * t;
				float wy = cam->y + dy * t;
				float tu = fractf(wx);
				float tv = fractf(wy);
				uint32_t c = ceil_tex ? texture_sample_nearest(ceil_tex, tu, tv) : 0xFF0B0E14u;
				c = lighting_apply(c, row_dist, plane_sector_intensity, plane_sector_tint, world->lights, world->light_count, wx, wy);
				fb->pixels[y * fb->width + x] = c;
			}
		}

		// Wall slice
		for (int y = y0c; y < y1c; y++) {
			float v = (float)(y - y0) / (float)(slice_h ? slice_h : 1);
			uint32_t c = tex ? texture_sample_nearest(tex, u, v) : base;
			c = lighting_apply(c, dist, wall_sector_intensity, wall_sector_tint, world->lights, world->light_count, hit_x, hit_y);
			fb->pixels[y * fb->width + x] = c;
		}

		// Floor (textured)
		if (y1c < fb->height) {
			for (int y = y1c; y < fb->height; y++) {
				float denom = (float)y - half_h;
				if (denom <= 0.001f) {
					continue;
				}
				float row_dist = proj_z / denom;
				float t = row_dist / (corr > 0.001f ? corr : 0.001f);
				float wx = cam->x + dx * t;
				float wy = cam->y + dy * t;
				float tu = fractf(wx);
				float tv = fractf(wy);
				uint32_t c = floor_tex ? texture_sample_nearest(floor_tex, tu, tv) : 0xFF121018u;
				c = lighting_apply(c, row_dist, plane_sector_intensity, plane_sector_tint, world->lights, world->light_count, wx, wy);
				fb->pixels[y * fb->width + x] = c;
			}
		}
	}
}
