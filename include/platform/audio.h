#pragma once

// Sound effects (SFX) core API.
//
// This is intentionally separate from MIDI music (FluidSynth) playback.
// The SFX core owns an SDL audio device and mixes WAV samples into it.

#include <stdbool.h>
#include <stdint.h>

#include "assets/asset_paths.h"

// Opaque handle to a loaded WAV sample.
typedef struct SfxSampleId {
	uint16_t index;
	uint16_t generation;
} SfxSampleId;

// Opaque handle to an active voice.
typedef struct SfxVoiceId {
	uint16_t index;
	uint16_t generation;
} SfxVoiceId;

// Initializes the SFX core.
// If enable_audio is false, all calls become no-ops that return "success".
// freq/samples apply to the SDL audio device request.
bool sfx_init(const AssetPaths* paths, bool enable_audio, int freq, int samples);

// Shuts down the SFX core and releases audio resources.
void sfx_shutdown(void);

// Global output gain applied to all SFX voices. Clamped to [0,1].
void sfx_set_master_volume(float volume);

// Loads (or returns a cached) WAV sample from Assets/Sounds/Effects/<filename>.
// Returns {0,0} on failure.
SfxSampleId sfx_load_effect_wav(const char* filename);

// Loads (or returns a cached) WAV sample from Assets/Sounds/Menus/<filename>.
// Returns {0,0} on failure.
SfxSampleId sfx_load_menu_wav(const char* filename);

// Plays a sample. gain is linear [0,1].
// If looping is true, the sample loops until stopped.
// Returns {0,0} when audio is disabled or on failure.
SfxVoiceId sfx_play(SfxSampleId sample, float gain, bool looping);

// Updates a playing voice gain. Safe to call even if voice is invalid/stale.
void sfx_voice_set_gain(SfxVoiceId voice, float gain);

// Stops a playing voice. Safe to call even if voice is invalid/stale.
void sfx_voice_stop(SfxVoiceId voice);

