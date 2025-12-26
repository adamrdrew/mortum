#include "game/map_music.h"

#include "assets/asset_paths.h"
#include "assets/midi_player.h"

#include "core/log.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

static bool file_exists(const char* path) {
	if (!path || path[0] == '\0') {
		return false;
	}
	FILE* f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	fclose(f);
	return true;
}

void game_map_music_stop(char* prev_bgmusic, size_t prev_bgmusic_cap, char* prev_soundfont, size_t prev_soundfont_cap) {
	midi_stop();
	if (prev_bgmusic && prev_bgmusic_cap > 0) {
		prev_bgmusic[0] = '\0';
	}
	if (prev_soundfont && prev_soundfont_cap > 0) {
		prev_soundfont[0] = '\0';
	}
}

void game_map_music_maybe_start(
	const AssetPaths* paths,
	const MapLoadResult* map,
	bool map_ok,
	bool audio_enabled,
	bool music_enabled,
	char* prev_bgmusic,
	size_t prev_bgmusic_cap,
	char* prev_soundfont,
	size_t prev_soundfont_cap
) {
	if (!paths || !map || !map_ok || !audio_enabled || !music_enabled || !prev_bgmusic || !prev_soundfont) {
		return;
	}
	bool midi_exists = map->bgmusic[0] != '\0';
	bool sf_exists = map->soundfont[0] != '\0';
	if (!midi_exists || !sf_exists) {
		return;
	}

	bool same = (strcmp(map->bgmusic, prev_bgmusic) == 0) && (strcmp(map->soundfont, prev_soundfont) == 0);
	if (same && midi_is_playing()) {
		return;
	}

	midi_stop();
	char* midi_path = asset_path_join(paths, "Sounds/MIDI", map->bgmusic);
	char* sf_path = asset_path_join(paths, "Sounds/SoundFonts", map->soundfont);
	if (!midi_path || !sf_path) {
		log_warn("MIDI path allocation failed");
		free(midi_path);
		free(sf_path);
		return;
	}
	if (!file_exists(midi_path)) {
		log_warn("MIDI file not found: %s", midi_path);
		free(midi_path);
		free(sf_path);
		return;
	}
	if (!file_exists(sf_path)) {
		log_warn("SoundFont file not found: %s", sf_path);
		free(midi_path);
		free(sf_path);
		return;
	}
	if (midi_init(sf_path) == 0) {
		midi_play(midi_path);
		if (prev_bgmusic_cap > 0) {
			strncpy(prev_bgmusic, map->bgmusic, prev_bgmusic_cap);
			prev_bgmusic[prev_bgmusic_cap - 1] = '\0';
		}
		if (prev_soundfont_cap > 0) {
			strncpy(prev_soundfont, map->soundfont, prev_soundfont_cap);
			prev_soundfont[prev_soundfont_cap - 1] = '\0';
		}
	} else {
		log_warn("Failed to init MIDI with SoundFont: %s", sf_path);
	}
	free(midi_path);
	free(sf_path);
}
