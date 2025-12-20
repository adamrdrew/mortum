#include "render/lighting.h"

#include <math.h>

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

LightColor light_color_white(void) {
	LightColor c;
	c.r = 1.0f;
	c.g = 1.0f;
	c.b = 1.0f;
	return c;
}

float lighting_distance_falloff(float dist) {
	// Matches the project’s existing wall shading curve (simple and readable).
	// dist is expected to be the raycaster’s perpendicular-corrected distance.
	if (dist < 0.0f) {
		dist = 0.0f;
	}
	return 1.0f / (1.0f + dist * 0.15f);
}

static float dist2_2d(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

uint32_t lighting_apply(
	uint32_t rgba,
	float dist,
	float sector_intensity,
	LightColor sector_tint,
	const PointLight* lights,
	int light_count,
	float sample_x,
	float sample_y) {
	float falloff = lighting_distance_falloff(dist);
	sector_intensity = clampf(sector_intensity, 0.0f, 1.0f);

	// Base per-channel multipliers.
	float r_mul = falloff * sector_intensity * clampf(sector_tint.r, 0.0f, 1.0f);
	float g_mul = falloff * sector_intensity * clampf(sector_tint.g, 0.0f, 1.0f);
	float b_mul = falloff * sector_intensity * clampf(sector_tint.b, 0.0f, 1.0f);

	// Additive point lights (in multiplier space).
	if (lights && light_count > 0) {
		for (int i = 0; i < light_count; i++) {
			const PointLight* L = &lights[i];
			if (L->radius <= 0.0f || L->intensity <= 0.0f) {
				continue;
			}
			float r2 = L->radius * L->radius;
			float d2 = dist2_2d(sample_x, sample_y, L->x, L->y);
			if (d2 >= r2) {
				continue;
			}
			float d = sqrtf(d2);
			float t = 1.0f - (d / L->radius);
			float a = L->intensity * clampf(t, 0.0f, 1.0f);

			float lr = clampf(L->color.r, 0.0f, 1.0f);
			float lg = clampf(L->color.g, 0.0f, 1.0f);
			float lb = clampf(L->color.b, 0.0f, 1.0f);

			r_mul += a * lr;
			g_mul += a * lg;
			b_mul += a * lb;
		}
	}

	// Clamp multipliers to avoid totally black frames; keep retro readability.
	r_mul = clampf(r_mul, 0.06f, 1.0f);
	g_mul = clampf(g_mul, 0.06f, 1.0f);
	b_mul = clampf(b_mul, 0.06f, 1.0f);

	uint8_t a = (uint8_t)((rgba >> 24) & 0xFF);
	uint8_t r = (uint8_t)((rgba >> 16) & 0xFF);
	uint8_t g = (uint8_t)((rgba >> 8) & 0xFF);
	uint8_t b = (uint8_t)(rgba & 0xFF);

	int rr = (int)lroundf((float)r * r_mul);
	int gg = (int)lroundf((float)g * g_mul);
	int bb = (int)lroundf((float)b * b_mul);

	return ((uint32_t)a << 24) | ((uint32_t)clamp_u8(rr) << 16) | ((uint32_t)clamp_u8(gg) << 8) | (uint32_t)clamp_u8(bb);
}
