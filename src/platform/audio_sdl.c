#include "platform/audio.h"
#include "core/log.h"

#include "game/tuning.h"

#include <SDL.h>

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// --- Internal types ---

typedef struct SfxSample {
	bool alive;
	uint16_t generation;
	char filename[64];
	float* frames_f32; // interleaved stereo float32
	uint32_t frame_count;
} SfxSample;

typedef struct SfxVoice {
	bool alive;
	uint16_t generation;
	uint32_t seq;
	uint16_t sample_index;
	uint32_t frame_pos;
	float gain;
	bool looping;
} SfxVoice;

typedef struct SfxCore {
	bool enabled;
	bool initialized;
	AssetPaths paths;
	float master_volume;

	SDL_AudioDeviceID dev;
	SDL_AudioSpec have;

	SfxSample samples[SFX_MAX_SAMPLES];
	SfxVoice voices[SFX_MAX_VOICES];
	uint32_t voice_seq;
} SfxCore;

static SfxCore g_sfx;

static float clamp01(float v) {
	if (v < 0.0f) {
		return 0.0f;
	}
	if (v > 1.0f) {
		return 1.0f;
	}
	return v;
}

static bool sfx_id_is_valid_sample(SfxSampleId id) {
	return id.index != 0u || id.generation != 0u;
}

static bool sfx_id_is_valid_voice(SfxVoiceId id) {
	return id.index != 0u || id.generation != 0u;
}

static SfxSampleId sfx_make_sample_id(uint16_t index, uint16_t generation) {
	SfxSampleId out;
	out.index = index;
	out.generation = generation;
	return out;
}

static SfxVoiceId sfx_make_voice_id(uint16_t index, uint16_t generation) {
	SfxVoiceId out;
	out.index = index;
	out.generation = generation;
	return out;
}

static uint16_t sfx_find_sample_slot_by_name(const char* filename) {
	for (uint16_t i = 0; i < (uint16_t)SFX_MAX_SAMPLES; i++) {
		SfxSample* s = &g_sfx.samples[i];
		if (!s->alive) {
			continue;
		}
		if (strncmp(s->filename, filename, sizeof(s->filename)) == 0) {
			return i;
		}
	}
	return UINT16_MAX;
}

static uint16_t sfx_find_free_sample_slot(void) {
	for (uint16_t i = 0; i < (uint16_t)SFX_MAX_SAMPLES; i++) {
		if (!g_sfx.samples[i].alive) {
			return i;
		}
	}
	return UINT16_MAX;
}

static uint16_t sfx_find_free_voice_slot(void) {
	for (uint16_t i = 0; i < (uint16_t)SFX_MAX_VOICES; i++) {
		if (!g_sfx.voices[i].alive) {
			return i;
		}
	}
	return UINT16_MAX;
}

static uint16_t sfx_find_oldest_voice_slot(void) {
	uint16_t best = UINT16_MAX;
	uint32_t best_seq = 0;
	for (uint16_t i = 0; i < (uint16_t)SFX_MAX_VOICES; i++) {
		SfxVoice* v = &g_sfx.voices[i];
		if (!v->alive) {
			continue;
		}
		if (best == UINT16_MAX || v->seq < best_seq) {
			best = i;
			best_seq = v->seq;
		}
	}
	return best;
}

static void SDLCALL sfx_audio_callback(void* userdata, Uint8* stream, int len) {
	(void)userdata;
	if (!stream || len <= 0) {
		return;
	}

	// We request AUDIO_F32 stereo; so stream is float32 interleaved.
	float* out = (float*)stream;
	const int out_floats = len / (int)sizeof(float);
	const int out_frames = out_floats / 2;
	if (out_frames <= 0) {
		return;
	}
	memset(out, 0, (size_t)len);

	const float master = g_sfx.master_volume;
	for (uint16_t vi = 0; vi < (uint16_t)SFX_MAX_VOICES; vi++) {
		SfxVoice* v = &g_sfx.voices[vi];
		if (!v->alive) {
			continue;
		}
		if (v->sample_index >= (uint16_t)SFX_MAX_SAMPLES) {
			v->alive = false;
			continue;
		}
		SfxSample* s = &g_sfx.samples[v->sample_index];
		if (!s->alive || !s->frames_f32 || s->frame_count == 0) {
			v->alive = false;
			continue;
		}

		float gain = clamp01(v->gain) * master;
		if (gain <= 0.0001f) {
			continue;
		}

		uint32_t pos = v->frame_pos;
		for (int of = 0; of < out_frames; of++) {
			if (pos >= s->frame_count) {
				if (v->looping) {
					pos = 0;
				} else {
					v->alive = false;
					break;
				}
			}
			const uint32_t si = pos * 2;
			const uint32_t oi = (uint32_t)of * 2;
			out[oi + 0] += s->frames_f32[si + 0] * gain;
			out[oi + 1] += s->frames_f32[si + 1] * gain;
			pos++;
		}
		v->frame_pos = pos;
	}

	// Clamp to avoid NaNs/overflows. Keep it simple.
	for (int i = 0; i < out_floats; i++) {
		float x = out[i];
		if (x > 1.0f) {
			x = 1.0f;
		} else if (x < -1.0f) {
			x = -1.0f;
		}
		out[i] = x;
	}
}

bool sfx_init(const AssetPaths* paths, bool enable_audio, int freq, int samples) {
	memset(&g_sfx, 0, sizeof(g_sfx));
	g_sfx.master_volume = 1.0f;
	g_sfx.enabled = enable_audio;

	if (paths) {
		// Shallow-copy AssetPaths; assets_root is an owned string in the caller,
		// but it stays alive for app lifetime in current engine.
		g_sfx.paths = *paths;
	}

	if (!enable_audio) {
		g_sfx.initialized = true;
		return true;
	}

	SDL_AudioSpec want;
	SDL_zero(want);
	if (freq < 8000) {
		freq = 8000;
	} else if (freq > 192000) {
		freq = 192000;
	}
	if (samples < 128) {
		samples = 128;
	} else if (samples > 8192) {
		samples = 8192;
	}
	want.freq = freq;
	want.format = AUDIO_F32;
	want.channels = 2;
	want.samples = (Uint16)samples;
	want.callback = sfx_audio_callback;
	want.userdata = NULL;

	SDL_zero(g_sfx.have);
	g_sfx.dev = SDL_OpenAudioDevice(NULL, 0, &want, &g_sfx.have, 0);
	if (g_sfx.dev == 0) {
		log_warn("SDL_OpenAudioDevice failed (SFX disabled): %s", SDL_GetError());
		g_sfx.enabled = false;
		g_sfx.initialized = true;
		return true; // non-fatal
	}

	SDL_PauseAudioDevice(g_sfx.dev, 0);
	g_sfx.initialized = true;
	return true;
}

void sfx_shutdown(void) {
	if (!g_sfx.initialized) {
		return;
	}
	if (g_sfx.dev != 0) {
		SDL_LockAudioDevice(g_sfx.dev);
		for (uint16_t i = 0; i < (uint16_t)SFX_MAX_VOICES; i++) {
			g_sfx.voices[i].alive = false;
		}
		SDL_UnlockAudioDevice(g_sfx.dev);
		SDL_CloseAudioDevice(g_sfx.dev);
		g_sfx.dev = 0;
	}

	for (uint16_t i = 0; i < (uint16_t)SFX_MAX_SAMPLES; i++) {
		SfxSample* s = &g_sfx.samples[i];
		if (!s->alive) {
			continue;
		}
		SDL_free(s->frames_f32);
		s->frames_f32 = NULL;
		s->frame_count = 0;
		s->alive = false;
		s->generation++;
	}

	memset(&g_sfx, 0, sizeof(g_sfx));
}

void sfx_set_master_volume(float volume) {
	if (!g_sfx.initialized) {
		return;
	}
	g_sfx.master_volume = clamp01(volume);
}

SfxSampleId sfx_load_effect_wav(const char* filename) {
	if (!g_sfx.initialized || !g_sfx.enabled || !filename || filename[0] == '\0') {
		return sfx_make_sample_id(0, 0);
	}

	uint16_t existing = sfx_find_sample_slot_by_name(filename);
	if (existing != UINT16_MAX) {
		SfxSample* s = &g_sfx.samples[existing];
		return sfx_make_sample_id(existing, s->generation);
	}

	uint16_t slot = sfx_find_free_sample_slot();
	if (slot == UINT16_MAX) {
		log_warn("SFX sample cache full (max %d): %s", (int)SFX_MAX_SAMPLES, filename);
		return sfx_make_sample_id(0, 0);
	}

	char* full = asset_path_join(&g_sfx.paths, "Sounds/Effects", filename);
	if (!full) {
		return sfx_make_sample_id(0, 0);
	}

	SDL_AudioSpec src;
	SDL_zero(src);
	Uint8* src_buf = NULL;
	Uint32 src_len = 0;
	if (!SDL_LoadWAV(full, &src, &src_buf, &src_len)) {
		log_warn("SDL_LoadWAV failed for %s: %s", full, SDL_GetError());
		free(full);
		return sfx_make_sample_id(0, 0);
	}
	free(full);

	SDL_AudioSpec dst;
	SDL_zero(dst);
	dst.freq = g_sfx.have.freq ? g_sfx.have.freq : 48000;
	dst.format = AUDIO_F32;
	dst.channels = 2;

	SDL_AudioCVT cvt;
	if (SDL_BuildAudioCVT(&cvt, src.format, src.channels, src.freq, dst.format, dst.channels, dst.freq) < 0) {
		log_error("SDL_BuildAudioCVT failed for %s: %s", filename, SDL_GetError());
		SDL_FreeWAV(src_buf);
		return sfx_make_sample_id(0, 0);
	}

	cvt.len = (int)src_len;
	cvt.buf = (Uint8*)SDL_malloc((size_t)cvt.len * (size_t)cvt.len_mult);
	if (!cvt.buf) {
		log_error("Out of memory converting WAV: %s", filename);
		SDL_FreeWAV(src_buf);
		return sfx_make_sample_id(0, 0);
	}
	memcpy(cvt.buf, src_buf, src_len);
	if (SDL_ConvertAudio(&cvt) < 0) {
		log_error("SDL_ConvertAudio failed for %s: %s", filename, SDL_GetError());
		SDL_free(cvt.buf);
		SDL_FreeWAV(src_buf);
		return sfx_make_sample_id(0, 0);
	}
	SDL_FreeWAV(src_buf);

	const uint32_t float_count = (uint32_t)(cvt.len_cvt / (int)sizeof(float));
	const uint32_t frame_count = float_count / 2u;
	if (frame_count == 0) {
		SDL_free(cvt.buf);
		return sfx_make_sample_id(0, 0);
	}

	SfxSample* s = &g_sfx.samples[slot];
	uint16_t gen = (uint16_t)(s->generation + 1u);
	memset(s, 0, sizeof(*s));
	s->alive = true;
	s->generation = gen;
	strncpy(s->filename, filename, sizeof(s->filename) - 1);
	s->filename[sizeof(s->filename) - 1] = '\0';
	s->frames_f32 = (float*)cvt.buf;
	s->frame_count = frame_count;

	return sfx_make_sample_id(slot, s->generation);
}

SfxVoiceId sfx_play(SfxSampleId sample, float gain, bool looping) {
	if (!g_sfx.initialized || !g_sfx.enabled || !sfx_id_is_valid_sample(sample)) {
		return sfx_make_voice_id(0, 0);
	}
	if (sample.index >= (uint16_t)SFX_MAX_SAMPLES) {
		return sfx_make_voice_id(0, 0);
	}
	SfxSample* s = &g_sfx.samples[sample.index];
	if (!s->alive || s->generation != sample.generation) {
		return sfx_make_voice_id(0, 0);
	}

	if (g_sfx.dev == 0) {
		return sfx_make_voice_id(0, 0);
	}

	SDL_LockAudioDevice(g_sfx.dev);
	uint16_t slot = sfx_find_free_voice_slot();
	if (slot == UINT16_MAX) {
		slot = sfx_find_oldest_voice_slot();
	}
	if (slot == UINT16_MAX) {
		SDL_UnlockAudioDevice(g_sfx.dev);
		return sfx_make_voice_id(0, 0);
	}

	SfxVoice* v = &g_sfx.voices[slot];
	uint16_t gen = (uint16_t)(v->generation + 1u);
	memset(v, 0, sizeof(*v));
	v->alive = true;
	v->generation = gen;
	v->seq = ++g_sfx.voice_seq;
	v->sample_index = sample.index;
	v->frame_pos = 0;
	v->gain = clamp01(gain);
	v->looping = looping;
	SfxVoiceId out = sfx_make_voice_id(slot, v->generation);
	SDL_UnlockAudioDevice(g_sfx.dev);
	return out;
}

void sfx_voice_set_gain(SfxVoiceId voice, float gain) {
	if (!g_sfx.initialized || !g_sfx.enabled || !sfx_id_is_valid_voice(voice)) {
		return;
	}
	if (g_sfx.dev == 0 || voice.index >= (uint16_t)SFX_MAX_VOICES) {
		return;
	}
	SDL_LockAudioDevice(g_sfx.dev);
	SfxVoice* v = &g_sfx.voices[voice.index];
	if (v->alive && v->generation == voice.generation) {
		v->gain = clamp01(gain);
	}
	SDL_UnlockAudioDevice(g_sfx.dev);
}

void sfx_voice_stop(SfxVoiceId voice) {
	if (!g_sfx.initialized || !g_sfx.enabled || !sfx_id_is_valid_voice(voice)) {
		return;
	}
	if (g_sfx.dev == 0 || voice.index >= (uint16_t)SFX_MAX_VOICES) {
		return;
	}
	SDL_LockAudioDevice(g_sfx.dev);
	SfxVoice* v = &g_sfx.voices[voice.index];
	if (v->alive && v->generation == voice.generation) {
		v->alive = false;
	}
	SDL_UnlockAudioDevice(g_sfx.dev);
}
