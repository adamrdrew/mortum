// midi_player.c
// Fluidsynth MIDI playback integration for Mortum
#include "midi_player.h"
#include <fluidsynth.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static fluid_settings_t* settings = NULL;
static fluid_synth_t* synth = NULL;
static fluid_player_t* player = NULL;
static fluid_audio_driver_t* audio_driver = NULL;
static int music_playing = 0;

int midi_init(const char* soundfont_path) {
    FILE* sf = fopen(soundfont_path, "rb");
    if (!sf) {
        fprintf(stderr, "Error: SoundFont file not found: %s\n", soundfont_path);
        return -10;
    }
    fclose(sf);
    settings = new_fluid_settings();
    if (!settings) {
        fprintf(stderr, "Error: Could not create Fluidsynth settings\n");
        return -1;
    }
#ifdef __APPLE__
    fluid_settings_setstr(settings, "audio.driver", "coreaudio");
#endif
        // Set verbose logging for Fluidsynth
        fluid_settings_setnum(settings, "synth.verbose", 1.0);
        fluid_settings_setnum(settings, "audio.verbose", 1.0);
    synth = new_fluid_synth(settings);
    if (!synth) {
        fprintf(stderr, "Error: Could not create Fluidsynth synth\n");
        return -2;
    }
    audio_driver = new_fluid_audio_driver(settings, synth);
    if (!audio_driver) {
        fprintf(stderr, "Error: Could not create Fluidsynth audio driver\n");
        return -5;
    }
    fprintf(stderr, "Info: Fluidsynth audio driver started\n");
    if (fluid_synth_sfload(synth, soundfont_path, 1) < 0) {
        fprintf(stderr, "Error: Could not load SoundFont: %s\n", soundfont_path);
        return -3;
    }
        // Set synth gain/volume to maximum
        fluid_synth_set_gain(synth, 1.0f);
        fprintf(stderr, "Info: Fluidsynth synth gain set to 1.0\n");
    player = new_fluid_player(synth);
    if (!player) {
        fprintf(stderr, "Error: Could not create Fluidsynth player\n");
        return -4;
    }
        // Set player volume to maximum (if supported)
        // Note: Fluidsynth does not have a direct player volume, but gain above applies to synth
        fprintf(stderr, "Info: Fluidsynth player created. SoundFont: %s\n", soundfont_path);
    return 0;
}

int midi_play(const char* midi_path) {
    if (!player || !synth) {
        fprintf(stderr, "Error: Fluidsynth not initialized\n");
        return -1;
    }
    // Stop any currently playing track before starting a new one
    if (music_playing) {
        fluid_player_stop(player);
        music_playing = 0;
    }
    FILE* mf = fopen(midi_path, "rb");
    if (!mf) {
        fprintf(stderr, "Error: MIDI file not found: %s\n", midi_path);
        return -10;
    }
    fclose(mf);
    if (fluid_player_add(player, midi_path) != FLUID_OK) {
        fprintf(stderr, "Error: Could not add MIDI file: %s\n", midi_path);
        return -11;
    }
    // Set player to loop indefinitely
    fluid_player_set_loop(player, 1);
    int play_result = fluid_player_play(player);
    if (play_result != FLUID_OK) {
        fprintf(stderr, "Error: Could not start MIDI playback: %s (code %d)\n", midi_path, play_result);
        return -12;
    } else {
        fprintf(stderr, "Info: MIDI playback started: %s\n", midi_path);
    }
    music_playing = 1;
    return 0;
}

void midi_stop(void) {
    if (player) fluid_player_stop(player);
    music_playing = 0;
}

void midi_shutdown(void) {
    midi_stop();
    if (player) delete_fluid_player(player);
    if (audio_driver) delete_fluid_audio_driver(audio_driver);
    if (synth) delete_fluid_synth(synth);
    if (settings) delete_fluid_settings(settings);
    player = NULL; synth = NULL; settings = NULL; audio_driver = NULL;
}

int midi_is_playing(void) {
    return music_playing;
}
