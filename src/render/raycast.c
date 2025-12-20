#include "render/raycast.h"

#include "render/draw.h"

#include <math.h>
#include <stdint.h>

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

static uint32_t shade(uint32_t rgba, float s) {
	s = clampf(s, 0.1f, 1.0f);
	uint8_t a = (uint8_t)((rgba >> 24) & 0xFF);
	uint8_t r = (uint8_t)((rgba >> 16) & 0xFF);
	uint8_t g = (uint8_t)((rgba >> 8) & 0xFF);
	uint8_t b = (uint8_t)(rgba & 0xFF);
	r = (uint8_t)((float)r * s);
	g = (uint8_t)((float)g * s);
	b = (uint8_t)((float)b * s);
	return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
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

		uint32_t base = 0xFFB0B0B0u;
		float s = 1.0f / (1.0f + dist * 0.15f);
		uint32_t c = shade(base, s);

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
	// Background: sky + floor
	draw_clear(fb, 0xFF0B0E14u);
	draw_rect(fb, 0, fb->height / 2, fb->width, fb->height / 2, 0xFF121018u);

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
		float s = 1.0f / (1.0f + dist * 0.15f);
		uint32_t shade_c = shade(base, s);

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

		for (int y = y0c; y < y1c; y++) {
			float v = (float)(y - y0) / (float)(slice_h ? slice_h : 1);
			uint32_t c = tex ? texture_sample_nearest(tex, u, v) : shade_c;
			// Apply distance shading
			c = shade(c, s);
			fb->pixels[y * fb->width + x] = c;
		}
	}
}
