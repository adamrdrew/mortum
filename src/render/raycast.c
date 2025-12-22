#include "render/raycast.h"

#include "render/draw.h"
#include "render/lighting.h"

#include "platform/time.h"

#include "game/world.h"

#include <math.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

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
		uint32_t c = lighting_apply(base, dist, sector_intensity, sector_tint, world->lights, world->light_count, hit_x, hit_y);

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

static void draw_sector_ceiling_column(
	Framebuffer* fb,
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
		for (int y = cy0; y < cy1; y++) {
			float denom = half_h - (float)y;
			if (denom <= 0.001f) {
				continue;
			}
			float row_dist = ((ceil_z - cam_z) * proj_dist) / denom;
			float t = row_dist / corr_safe;
			float wx = cam_x + dx * t;
			float wy = cam_y + dy * t;
			float tu = fractf(wx * plane_uv_scale);
			float tv = fractf(wy * plane_uv_scale);
			uint32_t c = ceil_tex ? texture_sample_nearest(ceil_tex, tu, tv) : 0xFF0B0E14u;
			c = lighting_apply(c, row_dist, sector_intensity, sector_tint, lights, light_count, wx, wy);
			fb->pixels[y * fb->width + x] = c;
		}
	}
}

static void draw_sector_floor_column(
	Framebuffer* fb,
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
		for (int y = fy0; y < fy1; y++) {
			float denom = (float)y - half_h;
			if (denom <= 0.001f) {
				continue;
			}
			float row_dist = ((cam_z - floor_z) * proj_dist) / denom;
			float t = row_dist / corr_safe;
			float wx = cam_x + dx * t;
			float wy = cam_y + dy * t;
			float tu = fractf(wx * plane_uv_scale);
			float tv = fractf(wy * plane_uv_scale);
			uint32_t c = floor_tex ? texture_sample_nearest(floor_tex, tu, tv) : 0xFF121018u;
			c = lighting_apply(c, row_dist, sector_intensity, sector_tint, lights, light_count, wx, wy);
			fb->pixels[y * fb->width + x] = c;
		}
	}
}

static void render_wall_span_textured(
	Framebuffer* fb,
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
	float u,
	const Texture* tex,
	uint32_t base,
	float dist,
	float light_intensity,
	LightColor light_tint,
	const PointLight* lights,
	int light_count,
	float hit_x,
	float hit_y,
	RaycastPerf* perf
) {
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
	float z_span = z_top - z_bot;
	if (fabsf(z_span) < 1e-6f) {
		z_span = 1.0f;
	}
	float inv_proj = proj_dist > 1e-6f ? (1.0f / proj_dist) : 1.0f;
	for (int y = y_top; y < y_bot; y++) {
		// Perspective-correct wall V: derive world z at this pixel from projection.
		float yf = (float)y + 0.5f;
		float z = cam_z + (half_h - yf) * dist * inv_proj;
		float v = (z_top - z) / z_span;
		v = clampf(v, 0.0f, 1.0f);
		uint32_t c = tex ? texture_sample_nearest(tex, u, v) : base;
		c = lighting_apply(c, dist, light_intensity, light_tint, lights, light_count, hit_x, hit_y);
		fb->pixels[y * fb->width + x] = c;
	}
}

static void render_column_textured_recursive(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const Texture* sky_tex,
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

	// Fill planes for this sector first; portals will overwrite the open span.
	double planes_t0 = 0.0;
	if (perf) {
		planes_t0 = platform_time_seconds();
	}
	draw_sector_ceiling_column(
		fb,
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
		s->ceil_z,
		ceil_tex,
		(ceil_is_sky ? sky_tex : NULL),
		sector_intensity,
		sector_tint,
		world->lights,
		world->light_count,
		perf
	);
	draw_sector_floor_column(
		fb,
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
		floor_tex,
		sector_intensity,
		sector_tint,
		world->lights,
		world->light_count,
		perf
	);
	if (perf) {
		perf->planes_ms += (platform_time_seconds() - planes_t0) * 1000.0;
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
		return;
	}

	const Wall* w = &world->walls[hit_wall];
	float corr_safe = corr > 0.001f ? corr : 0.001f;
	float dist = hit_t * corr_safe;
	if (out_depth) {
		// Keep nearest depth for the column.
		if (out_depth[x] > dist) {
			out_depth[x] = dist;
		}
	}

	// Compute hit u along segment
	Vertex a = world->vertices[w->v0];
	Vertex b = world->vertices[w->v1];
	float hit_x = cam->x + ray_dx * hit_t;
	float hit_y = cam->y + ray_dy * hit_t;
	float u = segment_u(a.x, a.y, b.x, b.y, hit_x, hit_y);
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
		double walls_t0 = 0.0;
		if (perf) {
			walls_t0 = platform_time_seconds();
		}
		int y_top = project_y(half_h, proj_dist, cam_z, s->ceil_z, dist);
		int y_bot = project_y(half_h, proj_dist, cam_z, s->floor_z, dist);
		render_wall_span_textured(
			fb,
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
			wall_tex,
			base,
			dist,
			sector_intensity,
			sector_tint,
			world->lights,
			world->light_count,
			hit_x,
			hit_y,
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
			wall_tex,
			base,
			dist,
			sector_intensity,
			sector_tint,
			world->lights,
			world->light_count,
			hit_x,
			hit_y,
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
			wall_tex,
			base,
			dist,
			sector_intensity,
			sector_tint,
			world->lights,
			world->light_count,
			hit_x,
			hit_y,
			perf
		);
		if (perf) {
			perf->walls_ms += (platform_time_seconds() - walls_t0) * 1000.0;
		}
	}

	float z_open_top = s->ceil_z < so->ceil_z ? s->ceil_z : so->ceil_z;
	float z_open_bot = s->floor_z > so->floor_z ? s->floor_z : so->floor_z;
	if (z_open_top > z_open_bot + 1e-4f) {
		int y_open_top = project_y(half_h, proj_dist, cam_z, z_open_top, dist);
		int y_open_bot = project_y(half_h, proj_dist, cam_z, z_open_bot, dist);
		int y0 = y_open_top;
		int y1 = y_open_bot;
		if (y0 < y_clip_top) {
			y0 = y_clip_top;
		}
		if (y1 > y_clip_bot) {
			y1 = y_clip_bot;
		}
		if (y0 < 0) {
			y0 = 0;
		}
		if (y1 > fb->height) {
			y1 = fb->height;
		}
		if (y0 < y1) {
			render_column_textured_recursive(
				fb,
				world,
				cam,
				texreg,
				paths,
				sky_tex,
				x,
				half_h,
				proj_dist,
				cam_z,
				ray_dx,
				ray_dy,
				corr,
				other,
				y0,
				y1,
				hit_t + 1e-4f,
				hit_wall,
				depth + 1,
					out_depth,
					perf
			);
		}
	}
}


static void raycast_render_textured_from_sector_internal(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
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
	float* out_depth
) {
	raycast_render_textured_from_sector(fb, world, cam, texreg, paths, sky_filename, out_depth, -1);
}

void raycast_render_textured_from_sector(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	int start_sector
) {
	raycast_render_textured_from_sector_internal(fb, world, cam, texreg, paths, sky_filename, out_depth, start_sector, NULL);
}

void raycast_render_textured_from_sector_profiled(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	int start_sector,
	RaycastPerf* out_perf
) {
	raycast_render_textured_from_sector_internal(fb, world, cam, texreg, paths, sky_filename, out_depth, start_sector, out_perf);
}
