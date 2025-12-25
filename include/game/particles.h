#pragma once

#include <stdbool.h>
#include <stdint.h>

// World-owned particle pool.
// Particles are lightweight, pool-allocated, and always run their lifecycle to completion.
// Rendering is allowed to cull/occlude particles without affecting lifecycle.

#define PARTICLE_MAX_DEFAULT 4096

typedef enum ParticleShape {
	PARTICLE_SHAPE_SQUARE = 0,
	PARTICLE_SHAPE_CIRCLE = 1,
} ParticleShape;

typedef struct ParticleKeyframe {
	float opacity; // [0..1]
	float size;    // world units
	float r;
	float g;
	float b;
	float color_blend_opacity; // [0..1] only used for image particles; ignored for shape particles
	float off_x;
	float off_y;
	float off_z;
} ParticleKeyframe;

typedef struct Particle {
	bool alive;
	bool has_image;
	ParticleShape shape;
	char image[64]; // filename under Assets/Images/Particles/ (no path)

	uint32_t age_ms;
	uint32_t life_ms;

	// Spawn-time origin in world space (emitter position at spawn).
	float origin_x;
	float origin_y;
	float origin_z;

	// Keyframes (start/end values).
	ParticleKeyframe start;
	ParticleKeyframe end;

	// Offset jitter sampled once at spawn.
	float jitter_start_x;
	float jitter_start_y;
	float jitter_start_z;
	float jitter_end_x;
	float jitter_end_y;
	float jitter_end_z;

	// Optional discrete screen-space rotation.
	bool rotate_enabled;
	float rot_deg;
	float rot_step_deg;
	uint32_t rot_step_ms;
	uint32_t rot_accum_ms;
} Particle;

typedef struct Particles {
	bool initialized;
	int capacity;
	Particle* items; // owned, length=capacity
	int alive_count;

	// Per-frame stats (cleared by particles_begin_frame).
	uint32_t stats_spawned;
	uint32_t stats_dropped;
	uint32_t stats_drawn_particles;
	uint32_t stats_pixels_written;
} Particles;

bool particles_init(Particles* self, int capacity);
void particles_shutdown(Particles* self);
void particles_reset(Particles* self);

// Clears per-frame stats used by perf dumps.
// Call once per frame (typically at the start of the frame).
void particles_begin_frame(Particles* self);

// Advances all particles by dt_ms. Particles always advance even if later culled from rendering.
void particles_tick(Particles* self, uint32_t dt_ms);

// Spawns a particle into the pool. If the pool is full, the particle is dropped.
void particles_spawn(Particles* self, const Particle* p);

// Rendering API forward decls (kept here to avoid pulling render headers into World headers).
typedef struct Framebuffer Framebuffer;
typedef struct World World;
typedef struct Camera Camera;
typedef struct TextureRegistry TextureRegistry;
typedef struct AssetPaths AssetPaths;

// Draws all alive particles as sprite-like billboards.
// Occlusion behavior matches entity sprites:
// - `wall_depth` prevents drawing particles behind solid walls in a column.
// - `depth_pixels` prevents drawing particles behind already-rendered world pixels.
void particles_draw(
	Particles* self,
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	int start_sector,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const float* wall_depth,
	const float* depth_pixels);
