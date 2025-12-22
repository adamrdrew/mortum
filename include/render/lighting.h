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

typedef enum LightFlicker {
	LIGHT_FLICKER_NONE = 0,
	LIGHT_FLICKER_FLAME = 1,
	LIGHT_FLICKER_MALFUNCTION = 2,
} LightFlicker;

typedef struct PointLight {
	float x;
	float y;
	float z;
	float radius;
	// Base brightness/intensity in [0,+inf). Runtime flicker is applied at render time.
	float intensity;
	LightColor color;
	LightFlicker flicker;
	uint32_t seed;
} PointLight;

LightColor light_color_white(void);

// Computes per-channel lighting multipliers in [0,1] (not applied to a pixel).
// This is useful for Gouraud/per-vertex style shading where you want to compute
// lighting at multiple sample points and interpolate.
LightColor lighting_compute_multipliers(
	float dist,
	float sector_intensity,
	LightColor sector_tint,
	const PointLight* lights,
	int light_count,
	float sample_x,
	float sample_y);

// Quantizes a scalar lighting factor in [0,1] to a small number of steps.
// This produces the classic PS1/N64-style banding.
float lighting_quantize_factor(float v);

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
