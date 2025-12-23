#include "game/sound_emitters.h"

#include "core/config.h"

#include "game/tuning.h"

#include <math.h>
#include <string.h>

static SoundEmitterId make_id(uint16_t index, uint16_t gen) {
	SoundEmitterId out;
	out.index = index;
	out.generation = gen;
	return out;
}

static bool id_valid(SoundEmitterId id) {
	return id.index != 0u || id.generation != 0u;
}

static float clamp01(float v) {
	if (v < 0.0f) {
		return 0.0f;
	}
	if (v > 1.0f) {
		return 1.0f;
	}
	return v;
}

static float attenuate_gain(bool spatial, float base_gain, float ex, float ey, float lx, float ly) {
	base_gain = clamp01(base_gain);
	if (!spatial) {
		return base_gain;
	}
	const CoreConfig* cfg = core_config_get();
	const float min_d = cfg ? cfg->audio.sfx_atten_min_dist : SFX_ATTEN_MIN_DIST;
	const float max_d = cfg ? cfg->audio.sfx_atten_max_dist : SFX_ATTEN_MAX_DIST;
	if (max_d <= min_d) {
		return base_gain;
	}
	float dx = ex - lx;
	float dy = ey - ly;
	float d = sqrtf(dx * dx + dy * dy);
	if (d <= min_d) {
		return base_gain;
	}
	if (d >= max_d) {
		return 0.0f;
	}
	float t = (max_d - d) / (max_d - min_d);
	if (t < 0.0f) {
		t = 0.0f;
	} else if (t > 1.0f) {
		t = 1.0f;
	}
	return base_gain * (t * t);
}

void sound_emitters_init(SoundEmitters* self) {
	memset(self, 0, sizeof(*self));
	self->free_head = 0;
	for (uint16_t i = 0; i < (uint16_t)SOUND_EMITTER_MAX; i++) {
		self->alive[i] = false;
		self->generation[i] = 1;
		self->free_next[i] = (uint16_t)(i + 1u);
		self->loop_voice[i].index = 0;
		self->loop_voice[i].generation = 0;
		self->loop_sample[i].index = 0;
		self->loop_sample[i].generation = 0;
		self->loop_active[i] = false;
	}
	self->free_next[SOUND_EMITTER_MAX - 1] = UINT16_MAX;
	self->initialized = true;
}

void sound_emitters_shutdown(SoundEmitters* self) {
	if (!self || !self->initialized) {
		return;
	}
	// Stop any loops.
	for (uint16_t i = 0; i < (uint16_t)SOUND_EMITTER_MAX; i++) {
		if (self->alive[i] && self->loop_active[i]) {
			sfx_voice_stop(self->loop_voice[i]);
			self->loop_active[i] = false;
			self->loop_voice[i] = (SfxVoiceId){0, 0};
		}
	}
	memset(self, 0, sizeof(*self));
}

void sound_emitters_reset(SoundEmitters* self) {
	if (!self) {
		return;
	}
	sound_emitters_shutdown(self);
	sound_emitters_init(self);
}

SoundEmitterId sound_emitter_create(SoundEmitters* self, float x, float y, bool spatial, float base_gain) {
	if (!self || !self->initialized) {
		return make_id(0, 0);
	}
	uint16_t slot = self->free_head;
	if (slot == UINT16_MAX || slot >= (uint16_t)SOUND_EMITTER_MAX) {
		return make_id(0, 0);
	}
	self->free_head = self->free_next[slot];
	self->alive[slot] = true;
	self->x[slot] = x;
	self->y[slot] = y;
	self->spatial[slot] = spatial;
	self->base_gain[slot] = clamp01(base_gain);
	self->loop_active[slot] = false;
	self->loop_voice[slot] = (SfxVoiceId){0, 0};
	self->loop_sample[slot] = (SfxSampleId){0, 0};
	return make_id(slot, self->generation[slot]);
}

static bool resolve(SoundEmitters* self, SoundEmitterId id, uint16_t* out_index) {
	if (!self || !self->initialized || !id_valid(id)) {
		return false;
	}
	if (id.index >= (uint16_t)SOUND_EMITTER_MAX) {
		return false;
	}
	if (!self->alive[id.index]) {
		return false;
	}
	if (self->generation[id.index] != id.generation) {
		return false;
	}
	*out_index = id.index;
	return true;
}

void sound_emitter_destroy(SoundEmitters* self, SoundEmitterId id) {
	uint16_t i = 0;
	if (!resolve(self, id, &i)) {
		return;
	}
	if (self->loop_active[i]) {
		sfx_voice_stop(self->loop_voice[i]);
		self->loop_active[i] = false;
		self->loop_voice[i] = (SfxVoiceId){0, 0};
	}
	self->alive[i] = false;
	self->generation[i] = (uint16_t)(self->generation[i] + 1u);
	self->free_next[i] = self->free_head;
	self->free_head = i;
}

void sound_emitter_set_pos(SoundEmitters* self, SoundEmitterId id, float x, float y) {
	uint16_t i = 0;
	if (!resolve(self, id, &i)) {
		return;
	}
	self->x[i] = x;
	self->y[i] = y;
}

void sound_emitter_set_gain(SoundEmitters* self, SoundEmitterId id, float base_gain) {
	uint16_t i = 0;
	if (!resolve(self, id, &i)) {
		return;
	}
	self->base_gain[i] = clamp01(base_gain);
}

void sound_emitters_play_one_shot_at(
	SoundEmitters* self,
	const char* wav_filename,
	float x,
	float y,
	bool spatial,
	float base_gain,
	float listener_x,
	float listener_y) {
	(void)self;
	SfxSampleId s = sfx_load_effect_wav(wav_filename);
	if (s.index == 0 && s.generation == 0) {
		return;
	}
	float g = attenuate_gain(spatial, base_gain, x, y, listener_x, listener_y);
	(void)sfx_play(s, g, false);
}

void sound_emitter_start_loop(
	SoundEmitters* self,
	SoundEmitterId id,
	const char* wav_filename,
	float listener_x,
	float listener_y) {
	uint16_t i = 0;
	if (!resolve(self, id, &i)) {
		return;
	}
	SfxSampleId s = sfx_load_effect_wav(wav_filename);
	if (s.index == 0 && s.generation == 0) {
		return;
	}
	self->loop_sample[i] = s;
	float g = attenuate_gain(self->spatial[i], self->base_gain[i], self->x[i], self->y[i], listener_x, listener_y);
	self->loop_voice[i] = sfx_play(s, g, true);
	self->loop_active[i] = (self->loop_voice[i].index != 0u || self->loop_voice[i].generation != 0u);
}

void sound_emitter_stop_loop(SoundEmitters* self, SoundEmitterId id) {
	uint16_t i = 0;
	if (!resolve(self, id, &i)) {
		return;
	}
	if (!self->loop_active[i]) {
		return;
	}
	sfx_voice_stop(self->loop_voice[i]);
	self->loop_active[i] = false;
	self->loop_voice[i] = (SfxVoiceId){0, 0};
}

void sound_emitters_update(SoundEmitters* self, float listener_x, float listener_y) {
	if (!self || !self->initialized) {
		return;
	}
	for (uint16_t i = 0; i < (uint16_t)SOUND_EMITTER_MAX; i++) {
		if (!self->alive[i] || !self->loop_active[i]) {
			continue;
		}
		float g = attenuate_gain(self->spatial[i], self->base_gain[i], self->x[i], self->y[i], listener_x, listener_y);
		sfx_voice_set_gain(self->loop_voice[i], g);
	}
}
