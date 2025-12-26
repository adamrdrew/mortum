// midi_player.c
// Fluidsynth MIDI playback integration for Mortum
#include "midi_player.h"

#include "core/log.h"

#include <fluidsynth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fluid_settings_t* settings = NULL;
static fluid_synth_t* synth = NULL;
static fluid_player_t* player = NULL;
static fluid_audio_driver_t* audio_driver = NULL;
static int music_playing = 0;

static void midi_log_state(const char* where) {
    log_info_s(
        "midi",
        "%s: settings=%p synth=%p driver=%p player=%p playing=%d",
        (where ? where : "(unknown)"),
        (void*)settings,
        (void*)synth,
        (void*)audio_driver,
        (void*)player,
        music_playing
    );
}

int midi_init(const char* soundfont_path) {
	log_info_s("midi", "midi_init(soundfont_path=%s path_ptr=%p)", soundfont_path ? soundfont_path : "(null)", (void*)soundfont_path);
 	midi_log_state("midi_init:entry");
    // Idempotent init: if we're already initialized, tear down cleanly first.
    if (settings || synth || player || audio_driver) {
        log_warn_s("midi", "midi_init called while initialized; shutting down previous instance");
        midi_shutdown();
    }
    FILE* sf = fopen(soundfont_path, "rb");
    if (!sf) {
        log_error_s("midi", "SoundFont file not found: %s", soundfont_path ? soundfont_path : "(null)");
        return -10;
    }
    fclose(sf);
    settings = new_fluid_settings();
    if (!settings) {
        log_error_s("midi", "Could not create Fluidsynth settings");
        return -1;
    }
#ifdef __APPLE__
    fluid_settings_setstr(settings, "audio.driver", "coreaudio");
#endif
	// NOTE: Some FluidSynth builds don't expose "synth.verbose"/"audio.verbose" as numeric settings.
	// Avoid emitting noisy "Unknown numeric setting" errors by not setting them here.
    synth = new_fluid_synth(settings);
    if (!synth) {
        log_error_s("midi", "Could not create Fluidsynth synth");
        return -2;
    }
    audio_driver = new_fluid_audio_driver(settings, synth);
    if (!audio_driver) {
        log_error_s("midi", "Could not create Fluidsynth audio driver");
        return -5;
    }
    log_info_s("midi", "Fluidsynth audio driver started");
    if (fluid_synth_sfload(synth, soundfont_path, 1) < 0) {
        log_error_s("midi", "Could not load SoundFont: %s", soundfont_path ? soundfont_path : "(null)");
        return -3;
    }
        // Set synth gain/volume to maximum
        fluid_synth_set_gain(synth, 1.0f);
        log_info_s("midi", "Fluidsynth synth gain set to 1.0");
    player = new_fluid_player(synth);
    if (!player) {
        log_error_s("midi", "Could not create Fluidsynth player");
        return -4;
    }
        // Set player volume to maximum (if supported)
        // Note: Fluidsynth does not have a direct player volume, but gain above applies to synth
        log_info_s("midi", "Fluidsynth player created. SoundFont: %s", soundfont_path ? soundfont_path : "(null)");
 	midi_log_state("midi_init:exit");
    return 0;
}

int midi_play(const char* midi_path) {
	log_info_s("midi", "midi_play(midi_path=%s path_ptr=%p)", midi_path ? midi_path : "(null)", (void*)midi_path);
 	midi_log_state("midi_play:entry");
    if (!player || !synth) {
        log_error_s("midi", "Fluidsynth not initialized");
        return -1;
    }
    // Stop any currently playing track before starting a new one
    if (music_playing) {
        fluid_player_stop(player);
        music_playing = 0;
    }
    FILE* mf = fopen(midi_path, "rb");
    if (!mf) {
        log_error_s("midi", "MIDI file not found: %s", midi_path ? midi_path : "(null)");
        return -10;
    }
    fclose(mf);
    if (fluid_player_add(player, midi_path) != FLUID_OK) {
        log_error_s("midi", "Could not add MIDI file: %s", midi_path ? midi_path : "(null)");
        return -11;
    }
    // Set player to loop indefinitely
    fluid_player_set_loop(player, 1);
    int play_result = fluid_player_play(player);
    if (play_result != FLUID_OK) {
        log_error_s("midi", "Could not start MIDI playback: %s (code %d)", midi_path ? midi_path : "(null)", play_result);
        return -12;
    } else {
        log_info_s("midi", "MIDI playback started: %s", midi_path ? midi_path : "(null)");
    }
    music_playing = 1;
 	midi_log_state("midi_play:exit");
    return 0;
}

void midi_stop(void) {
	log_info_s("midi", "midi_stop()" );
	midi_log_state("midi_stop:entry");
    if (player) fluid_player_stop(player);
    music_playing = 0;
	midi_log_state("midi_stop:exit");
}

void midi_shutdown(void) {
	log_info_s("midi", "midi_shutdown()" );
	midi_log_state("midi_shutdown:entry");
    midi_stop();
    if (player) delete_fluid_player(player);
    if (audio_driver) delete_fluid_audio_driver(audio_driver);
    if (synth) delete_fluid_synth(synth);
    if (settings) delete_fluid_settings(settings);
    player = NULL; synth = NULL; settings = NULL; audio_driver = NULL;
	midi_log_state("midi_shutdown:exit");
}

int midi_is_playing(void) {
    return music_playing;
}
