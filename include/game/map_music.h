#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "assets/asset_paths.h"
#include "assets/map_loader.h"

// Starts map MIDI if configured and not already playing the same track.
// Maintains `prev_bgmusic`/`prev_soundfont` caches for "same track" detection.
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
);

// Stops MIDI and clears caches.
void game_map_music_stop(char* prev_bgmusic, size_t prev_bgmusic_cap, char* prev_soundfont, size_t prev_soundfont_cap);
