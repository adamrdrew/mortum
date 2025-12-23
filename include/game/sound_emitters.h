#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "platform/audio.h"

// SoundEmitter system.
//
// This is the single gameplay-facing path for SFX. Emitters can be:
// - spatial (world-space): their gain is attenuated based on listener distance
// - non-spatial (player/camera): no attenuation
//
// Emitters are cheap to create/destroy and safe to use via opaque handles.

#define SOUND_EMITTER_MAX 256

typedef struct SoundEmitterId {
	uint16_t index;
	uint16_t generation;
} SoundEmitterId;

typedef struct SoundEmitters {
	bool initialized;
	uint16_t free_head;
	uint16_t free_next[SOUND_EMITTER_MAX];
	uint16_t generation[SOUND_EMITTER_MAX];
	bool alive[SOUND_EMITTER_MAX];

	float x[SOUND_EMITTER_MAX];
	float y[SOUND_EMITTER_MAX];
	bool spatial[SOUND_EMITTER_MAX];
	float base_gain[SOUND_EMITTER_MAX];

	// Optional loop playback.
	SfxSampleId loop_sample[SOUND_EMITTER_MAX];
	SfxVoiceId loop_voice[SOUND_EMITTER_MAX];
	bool loop_active[SOUND_EMITTER_MAX];
} SoundEmitters;

void sound_emitters_init(SoundEmitters* self);
void sound_emitters_shutdown(SoundEmitters* self);

// Destroys all emitters (stopping any loops) and re-initializes the pool.
void sound_emitters_reset(SoundEmitters* self);

SoundEmitterId sound_emitter_create(SoundEmitters* self, float x, float y, bool spatial, float base_gain);
void sound_emitter_destroy(SoundEmitters* self, SoundEmitterId id);

void sound_emitter_set_pos(SoundEmitters* self, SoundEmitterId id, float x, float y);
void sound_emitter_set_gain(SoundEmitters* self, SoundEmitterId id, float base_gain);

// Plays a one-shot at an arbitrary position (no persistent emitter required).
void sound_emitters_play_one_shot_at(
	SoundEmitters* self,
	const char* wav_filename,
	float x,
	float y,
	bool spatial,
	float base_gain,
	float listener_x,
	float listener_y);

// Starts/stops a looping sound on an emitter.
void sound_emitter_start_loop(
	SoundEmitters* self,
	SoundEmitterId id,
	const char* wav_filename,
	float listener_x,
	float listener_y);

void sound_emitter_stop_loop(SoundEmitters* self, SoundEmitterId id);

// Updates looping emitters (recomputes gain from listener distance).
void sound_emitters_update(SoundEmitters* self, float listener_x, float listener_y);
