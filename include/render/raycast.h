#pragma once

#include "game/world.h"
#include "render/camera.h"
#include "render/framebuffer.h"

#include "render/texture.h"

typedef struct RaycastPerf {
	// Time spent inside raycaster (ms). Note: planes/walls timings include texture sampling + lighting.
	double planes_ms;
	double hit_test_ms;
	double walls_ms;

	// Lighting-specific perf (captured during perf trace only).
	double light_cull_ms; // time spent building visible light list (culling + flicker)
	uint32_t lights_in_world;
	uint32_t lights_visible_uncapped;
	uint32_t lights_visible_walls;
	uint32_t lights_visible_planes;
	uint64_t lighting_apply_calls;
	uint64_t lighting_apply_light_iters;
	uint64_t lighting_mul_calls;
	uint64_t lighting_mul_light_iters;

	// Texture registry work.
	double tex_lookup_ms;
	uint32_t texture_get_calls;
	uint32_t registry_string_compares;

	// Counters to help explain where time goes.
	uint32_t portal_calls;
	uint32_t portal_max_depth;
	uint32_t wall_ray_tests; // number of ray/segment tests performed
	uint32_t pixels_floor;
	uint32_t pixels_ceil;
	uint32_t pixels_wall;
} RaycastPerf;

// Debug/diagnostics: toggles processing of point-light emitters.
// When disabled, the renderer will not build visible-light lists and will not
// iterate point-light emitters at all.
void raycast_set_point_lights_enabled(bool enabled);

// Builds a per-frame list of visible point lights for the given camera.
// The returned lights include runtime flicker modulation and are capped similarly to the
// main textured wall lighting path.
// Returns the number of lights written to `out` (0 if point lights are disabled).
int raycast_build_visible_lights(PointLight* out, int out_cap, const World* world, const Camera* cam, float time_s);

// Untextured baseline raycast renderer.
void raycast_render_untextured(Framebuffer* fb, const World* world, const Camera* cam);

// Textured wall path (nearest sampling). Falls back to flat shading if texture missing.
// If `out_depth` is non-NULL, it must be at least fb->width floats and will be filled
// with the corrected wall distance for each column (or a large value if nothing hit).
// If `out_depth_pixels` is non-NULL, it must be at least fb->width * fb->height floats and
// will be filled with corrected depth for the nearest drawn world pixel at each screen pixel.
// If `sky_filename` is non-NULL and a sector has `ceil_tex` set to "SKY", the ceiling
// is rendered as a DOOM-style cylindrical sky panorama loaded from `Assets/Images/Sky/`.
void raycast_render_textured(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	float* out_depth_pixels
);

// Like raycast_render_textured, but uses the provided sector index as the recursion start sector.
// Pass -1 to fall back to automatic sector selection from (cam->x, cam->y).
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
);

// Profiled version: fills `out_perf` when non-NULL.
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
);
