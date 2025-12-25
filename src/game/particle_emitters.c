#include "game/particle_emitters.h"

#include "core/log.h"
#include "game/collision.h"
#include "game/world.h"

#include <math.h>
#include <string.h>

static inline float clampf2(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static inline uint32_t hash_u32(uint32_t x) {
	// Murmur3 finalizer-like mix (matches other files' style).
	x ^= x >> 16u;
	x *= 0x7FEB352Du;
	x ^= x >> 15u;
	x *= 0x846CA68Bu;
	x ^= x >> 16u;
	return x;
}

static float rand01(uint32_t seed) {
	uint32_t x = hash_u32(seed);
	// 24-bit mantissa.
	return (float)((x >> 8u) & 0x00FFFFFFu) / 16777215.0f;
}

static float rand_signed(uint32_t seed) {
	return rand01(seed) * 2.0f - 1.0f;
}

static void def_sanitize(ParticleEmitterDef* d) {
	if (!d) {
		return;
	}
	if (d->particle_life_ms < 1) {
		d->particle_life_ms = 1;
	}
	if (d->emit_interval_ms < 1) {
		d->emit_interval_ms = 1;
	}
	d->offset_jitter = fmaxf(d->offset_jitter, 0.0f);

	d->start.opacity = clampf2(d->start.opacity, 0.0f, 1.0f);
	d->end.opacity = clampf2(d->end.opacity, 0.0f, 1.0f);
	d->start.size = fmaxf(d->start.size, 0.0f);
	d->end.size = fmaxf(d->end.size, 0.0f);

	d->start.color.r = clampf2(d->start.color.r, 0.0f, 1.0f);
	d->start.color.g = clampf2(d->start.color.g, 0.0f, 1.0f);
	d->start.color.b = clampf2(d->start.color.b, 0.0f, 1.0f);
	d->end.color.r = clampf2(d->end.color.r, 0.0f, 1.0f);
	d->end.color.g = clampf2(d->end.color.g, 0.0f, 1.0f);
	d->end.color.b = clampf2(d->end.color.b, 0.0f, 1.0f);
	// Blend opacity is for images only, but keep it well-defined.
	d->start.color.opacity = clampf2(d->start.color.opacity, 0.0f, 1.0f);
	d->end.color.opacity = clampf2(d->end.color.opacity, 0.0f, 1.0f);

	if (d->rotate.enabled) {
		if (d->rotate.tick.time_ms < 1) {
			d->rotate.tick.time_ms = 1;
		}
	}
}

static bool particle_emitter_id_valid(const ParticleEmitters* self, ParticleEmitterId id) {
	if (!self || !self->initialized) {
		return false;
	}
	if (id.index >= PARTICLE_EMITTER_MAX) {
		return false;
	}
	if (!self->alive[id.index]) {
		return false;
	}
	return self->generation[id.index] == id.generation;
}

void particle_emitters_init(ParticleEmitters* self) {
	if (!self) {
		return;
	}
	memset(self, 0, sizeof(*self));
	self->initialized = true;
	self->alive_count = 0;
	self->stats_emitters_updated = 0u;
	self->stats_emitters_gated = 0u;
	self->stats_particles_spawn_attempted = 0u;
	self->free_head = 0;
	for (uint16_t i = 0; i < PARTICLE_EMITTER_MAX; i++) {
		self->free_next[i] = (uint16_t)(i + 1u);
		self->generation[i] = 1u;
		self->alive[i] = false;
		self->sector[i] = -1;
		self->last_valid_sector[i] = -1;
	}
	self->free_next[PARTICLE_EMITTER_MAX - 1] = (uint16_t)PARTICLE_EMITTER_MAX;
}

void particle_emitters_begin_frame(ParticleEmitters* self) {
	if (!self || !self->initialized) {
		return;
	}
	self->stats_emitters_updated = 0u;
	self->stats_emitters_gated = 0u;
	self->stats_particles_spawn_attempted = 0u;
}

void particle_emitters_shutdown(ParticleEmitters* self) {
	if (!self) {
		return;
	}
	memset(self, 0, sizeof(*self));
}

void particle_emitters_reset(ParticleEmitters* self) {
	particle_emitters_shutdown(self);
	particle_emitters_init(self);
}

ParticleEmitterId particle_emitter_create(
	ParticleEmitters* self,
	const World* world,
	float x,
	float y,
	float z,
	const ParticleEmitterDef* def) {
	ParticleEmitterId none;
	none.index = 0;
	none.generation = 0;
	if (!self || !self->initialized || !def) {
		return none;
	}
	if (self->free_head >= PARTICLE_EMITTER_MAX) {
		return none;
	}
	uint16_t idx = self->free_head;
	self->free_head = self->free_next[idx];

	self->alive[idx] = true;
	self->alive_count++;
	self->x[idx] = x;
	self->y[idx] = y;
	self->z[idx] = z;
	self->emit_accum_ms[idx] = 0u;
	self->spawn_counter[idx] = 0u;
	self->sector[idx] = -1;
	self->last_valid_sector[idx] = -1;
	self->def[idx] = *def;
	def_sanitize(&self->def[idx]);

	if (world) {
		int s = world_find_sector_at_point(world, x, y);
		self->sector[idx] = s;
		self->last_valid_sector[idx] = s;
	}

	ParticleEmitterId id;
	id.index = idx;
	id.generation = self->generation[idx];
	return id;
}

void particle_emitter_destroy(ParticleEmitters* self, ParticleEmitterId id) {
	if (!particle_emitter_id_valid(self, id)) {
		return;
	}
	uint16_t idx = id.index;
	self->alive[idx] = false;
	if (self->alive_count > 0) {
		self->alive_count--;
	}
	self->generation[idx]++;
	self->free_next[idx] = self->free_head;
	self->free_head = idx;
}

void particle_emitter_set_pos(
	ParticleEmitters* self,
	const World* world,
	ParticleEmitterId id,
	float x,
	float y,
	float z) {
	if (!particle_emitter_id_valid(self, id)) {
		return;
	}
	uint16_t idx = id.index;
	self->x[idx] = x;
	self->y[idx] = y;
	self->z[idx] = z;
	if (world) {
		int s = world_find_sector_at_point_stable(world, x, y, self->last_valid_sector[idx]);
		self->sector[idx] = s;
		self->last_valid_sector[idx] = s;
	}
}

static bool emitter_should_emit(
	const ParticleEmitters* self,
	const World* world,
	uint16_t idx,
	float player_x,
	float player_y,
	int player_sector) {
	(void)self;
	if (!world) {
		return false;
	}
	int es = self->sector[idx];
	if (es >= 0 && es == player_sector) {
		return true;
	}
	// Across sectors: allow emission only if solid-wall LOS exists.
	return collision_line_of_sight(world, self->x[idx], self->y[idx], player_x, player_y);
}

static void spawn_particle_from_emitter(Particles* particles, const ParticleEmitters* self, uint16_t idx) {
	const ParticleEmitterDef* d = &self->def[idx];
	Particle p;
	memset(&p, 0, sizeof(p));
	p.alive = true;
	p.life_ms = (uint32_t)d->particle_life_ms;
	p.age_ms = 0u;
	p.origin_x = self->x[idx];
	p.origin_y = self->y[idx];
	p.origin_z = self->z[idx];
	p.has_image = (d->image[0] != '\0');
	p.shape = d->shape;
	if (p.has_image) {
		strncpy(p.image, d->image, sizeof(p.image) - 1);
		p.image[sizeof(p.image) - 1] = '\0';
	}

	p.start.opacity = d->start.opacity;
	p.start.size = d->start.size;
	p.start.r = d->start.color.r;
	p.start.g = d->start.color.g;
	p.start.b = d->start.color.b;
	p.start.color_blend_opacity = d->start.color.opacity;
	p.start.off_x = d->start.offset.x;
	p.start.off_y = d->start.offset.y;
	p.start.off_z = d->start.offset.z;

	p.end.opacity = d->end.opacity;
	p.end.size = d->end.size;
	p.end.r = d->end.color.r;
	p.end.g = d->end.color.g;
	p.end.b = d->end.color.b;
	p.end.color_blend_opacity = d->end.color.opacity;
	p.end.off_x = d->end.offset.x;
	p.end.off_y = d->end.offset.y;
	p.end.off_z = d->end.offset.z;

	p.rotate_enabled = d->rotate.enabled;
	p.rot_deg = 0.0f;
	p.rot_step_deg = d->rotate.tick.deg;
	p.rot_step_ms = (uint32_t)d->rotate.tick.time_ms;
	p.rot_accum_ms = 0u;

	// Spawn-time offset jitter.
	float j = d->offset_jitter;
	if (j > 0.0f) {
		uint32_t seed0 = hash_u32((uint32_t)idx ^ (self->spawn_counter[idx] * 0x9E3779B9u));
		p.jitter_start_x = rand_signed(seed0 ^ 0xA511E9B3u) * j;
		p.jitter_start_y = rand_signed(seed0 ^ 0x94D049BBu) * j;
		p.jitter_start_z = rand_signed(seed0 ^ 0xD1B54A35u) * j;
		p.jitter_end_x = rand_signed(seed0 ^ 0xC2B2AE3Du) * j;
		p.jitter_end_y = rand_signed(seed0 ^ 0x165667B1u) * j;
		p.jitter_end_z = rand_signed(seed0 ^ 0x27D4EB2Fu) * j;
	}

	particles_spawn(particles, &p);
}

void particle_emitters_update(
	ParticleEmitters* self,
	const World* world,
	Particles* particles,
	float player_x,
	float player_y,
	int player_sector,
	uint32_t dt_ms) {
	if (!self || !self->initialized || !world || !particles || !particles->initialized) {
		return;
	}
	if (dt_ms == 0u) {
		return;
	}
	for (uint16_t i = 0; i < PARTICLE_EMITTER_MAX; i++) {
		if (!self->alive[i]) {
			continue;
		}
		self->stats_emitters_updated++;
		bool ok = emitter_should_emit(self, world, i, player_x, player_y, player_sector);
		if (!ok) {
			self->stats_emitters_gated++;
			// No backlog; when visibility returns, resume regular cadence.
			self->emit_accum_ms[i] = 0u;
			continue;
		}

		const ParticleEmitterDef* d = &self->def[i];
		uint32_t interval = (uint32_t)(d->emit_interval_ms > 0 ? d->emit_interval_ms : 1);
		self->emit_accum_ms[i] += dt_ms;
		uint32_t spawned = 0u;
		while (self->emit_accum_ms[i] >= interval) {
			self->emit_accum_ms[i] -= interval;
			spawn_particle_from_emitter(particles, self, i);
			self->spawn_counter[i]++;
			spawned++;
			self->stats_particles_spawn_attempted++;
			// Bound per-update work; tiny intervals relative to dt_ms could otherwise burst.
			if (spawned >= 16u) {
				break;
			}
		}
	}
}
