#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "game/particles.h"

#define PARTICLE_EMITTER_MAX 256

typedef struct ParticleVec3 {
	float x;
	float y;
	float z;
} ParticleVec3;

typedef struct ParticleEmitterColor {
	float r;
	float g;
	float b;
	float opacity; // [0..1] blend opacity (image particles only)
} ParticleEmitterColor;

typedef struct ParticleEmitterKeyframe {
	float opacity; // [0..1]
	ParticleEmitterColor color;
	float size; // world units
	ParticleVec3 offset;
} ParticleEmitterKeyframe;

typedef struct ParticleEmitterRotateTick {
	float deg;      // degrees per tick
	int time_ms;    // tick interval in ms
} ParticleEmitterRotateTick;

typedef struct ParticleEmitterRotate {
	bool enabled;
	ParticleEmitterRotateTick tick;
} ParticleEmitterRotate;

typedef struct ParticleEmitterDef {
	int particle_life_ms;
	int emit_interval_ms;
	float offset_jitter;
	ParticleEmitterRotate rotate;
	char image[64]; // optional filename under Assets/Images/Particles (no path). If non-empty, overrides shape.
	ParticleShape shape; // used when image is empty
	ParticleEmitterKeyframe start;
	ParticleEmitterKeyframe end;
} ParticleEmitterDef;

typedef struct ParticleEmitterId {
	uint16_t index;
	uint16_t generation;
} ParticleEmitterId;

typedef struct ParticleEmitters {
	bool initialized;
	uint16_t free_head;
	uint16_t free_next[PARTICLE_EMITTER_MAX];
	uint16_t generation[PARTICLE_EMITTER_MAX];
	bool alive[PARTICLE_EMITTER_MAX];
	int alive_count;

	// Per-frame stats (cleared by particle_emitters_begin_frame).
	uint32_t stats_emitters_updated;
	uint32_t stats_emitters_gated;
	uint32_t stats_particles_spawn_attempted;

	// Runtime state.
	float x[PARTICLE_EMITTER_MAX];
	float y[PARTICLE_EMITTER_MAX];
	float z[PARTICLE_EMITTER_MAX];
	int sector[PARTICLE_EMITTER_MAX];
	int last_valid_sector[PARTICLE_EMITTER_MAX];
	uint32_t emit_accum_ms[PARTICLE_EMITTER_MAX];
	uint32_t spawn_counter[PARTICLE_EMITTER_MAX];

	ParticleEmitterDef def[PARTICLE_EMITTER_MAX];
} ParticleEmitters;

typedef struct World World;

void particle_emitters_init(ParticleEmitters* self);
void particle_emitters_shutdown(ParticleEmitters* self);
void particle_emitters_reset(ParticleEmitters* self);

// Clears per-frame stats used by perf dumps.
// Call once per frame (typically at the start of the frame).
void particle_emitters_begin_frame(ParticleEmitters* self);

ParticleEmitterId particle_emitter_create(
	ParticleEmitters* self,
	const World* world,
	float x,
	float y,
	float z,
	const ParticleEmitterDef* def);

void particle_emitter_destroy(ParticleEmitters* self, ParticleEmitterId id);

void particle_emitter_set_pos(
	ParticleEmitters* self,
	const World* world,
	ParticleEmitterId id,
	float x,
	float y,
	float z);

void particle_emitters_update(
	ParticleEmitters* self,
	const World* world,
	Particles* particles,
	float player_x,
	float player_y,
	int player_sector,
	uint32_t dt_ms);
