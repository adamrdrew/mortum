#pragma once

#include <stdint.h>

// Simple, cheap lighting helpers for the software renderer.
//
// Model:
// - Distance falloff (darken with distance)
// - Sector light intensity + RGB tint multiplier
// - Optional point lights (additive per-channel brightening)
//
// All color factors are expected to be in [0, 1] (point lights can push above 1 before clamp).

typedef struct LightColor {
	float r;
	float g;
	float b;
} LightColor;

typedef struct PointLight {
	float x;
	float y;
	float z;
	float radius;
	float intensity;
	LightColor color;
} PointLight;

LightColor light_color_white(void);

// Applies lighting to an RGBA pixel.
// - dist: distance from camera to shaded surface/object (used for falloff)
// - sector_intensity: usually Sector.light in [0,1]
// - sector_tint: usually sector light tint (default white)
// - sample_x/y: world-space sample location (wall hit or object position)
uint32_t lighting_apply(
	uint32_t rgba,
	float dist,
	float sector_intensity,
	LightColor sector_tint,
	const PointLight* lights,
	int light_count,
	float sample_x,
	float sample_y);

// Computes the distance falloff factor used by lighting_apply.
float lighting_distance_falloff(float dist);
