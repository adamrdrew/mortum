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

static float quantize_factor(float v, int steps) {
	v = clampf(v, 0.0f, 1.0f);
	if (steps <= 1) {
		return v;
	}
	float s = (float)(steps - 1);
	return roundf(v * s) / s;
}

float lighting_distance_falloff(float dist) {
	// Aggressive fog-to-black, but with a readable near range.
	// 0..fog_start: no fog. fog_end+: fully black.
	if (dist < 0.0f) {
		dist = 0.0f;
	}
	const float fog_start = 6.0f;
	const float fog_end = 28.0f;
	if (dist <= fog_start) {
		return 1.0f;
	}
	if (dist >= fog_end) {
		return 0.0f;
	}
	float t = (fog_end - dist) / (fog_end - fog_start);
	// Ease in so the fade feels more like classic distance darkness.
	return t * t;
}

static float dist2_2d(float ax, float ay, float bx, float by) {
	float dx = ax - bx;
	float dy = ay - by;
	return dx * dx + dy * dy;
}

LightColor lighting_compute_multipliers(
	float dist,
	float sector_intensity,
	LightColor sector_tint,
	const PointLight* lights,
	int light_count,
	float sample_x,
	float sample_y
) {
	float fog = lighting_distance_falloff(dist);

	// Low ambient baseline (sector_intensity acts as an ambient knob).
	// Keep this readable up close, but let fog take it away at distance.
	float amb = clampf(sector_intensity, 0.0f, 1.0f);
	amb *= 0.45f;
	amb *= fog;

	float r_mul = amb * clampf(sector_tint.r, 0.0f, 1.0f);
	float g_mul = amb * clampf(sector_tint.g, 0.0f, 1.0f);
	float b_mul = amb * clampf(sector_tint.b, 0.0f, 1.0f);

	// Additive point lights (in multiplier space). These are also fogged by distance.
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
			// Slightly non-linear so lights feel punchy near the source.
			float a = L->intensity * clampf(t * t, 0.0f, 1.0f);

			float lr = clampf(L->color.r, 0.0f, 1.0f);
			float lg = clampf(L->color.g, 0.0f, 1.0f);
			float lb = clampf(L->color.b, 0.0f, 1.0f);

			r_mul += a * lr;
			g_mul += a * lg;
			b_mul += a * lb;
		}
	}

	LightColor out;
	// Raise the black level slightly ("lift"), but keep it fogged so distance still goes black.
	const float min_visibility = 0.185f;
	out.r = clampf(r_mul, 0.0f, 1.0f);
	out.g = clampf(g_mul, 0.0f, 1.0f);
	out.b = clampf(b_mul, 0.0f, 1.0f);
	float lifted_min = min_visibility * fog;
	if (out.r < lifted_min) {
		out.r = lifted_min;
	}
	if (out.g < lifted_min) {
		out.g = lifted_min;
	}
	if (out.b < lifted_min) {
		out.b = lifted_min;
	}
	return out;
}

float lighting_quantize_factor(float v) {
	// 16 steps is intentionally chunky and noticeable (PS1/N64 vibe), but don't crush
	// very low values to pure black (otherwise the near scene becomes unreadable).
	if (v < 0.08f) {
		return clampf(v, 0.0f, 1.0f);
	}
	return quantize_factor(v, 16);
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
	LightColor mul = lighting_compute_multipliers(dist, sector_intensity, sector_tint, lights, light_count, sample_x, sample_y);
	// Quantize for a PS1/N64-style banded look.
	float r_mul = lighting_quantize_factor(mul.r);
	float g_mul = lighting_quantize_factor(mul.g);
	float b_mul = lighting_quantize_factor(mul.b);

	uint8_t a = (uint8_t)((rgba >> 24) & 0xFF);
	uint8_t r = (uint8_t)((rgba >> 16) & 0xFF);
	uint8_t g = (uint8_t)((rgba >> 8) & 0xFF);
	uint8_t b = (uint8_t)(rgba & 0xFF);

	int rr = (int)lroundf((float)r * r_mul);
	int gg = (int)lroundf((float)g * g_mul);
	int bb = (int)lroundf((float)b * b_mul);

	return ((uint32_t)a << 24) | ((uint32_t)clamp_u8(rr) << 16) | ((uint32_t)clamp_u8(gg) << 8) | (uint32_t)clamp_u8(bb);
}
