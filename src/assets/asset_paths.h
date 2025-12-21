// asset_paths.h
#ifndef ASSET_PATHS_H
#define ASSET_PATHS_H
#include <stddef.h>
void get_midi_path(const char* midi_file, char* out, size_t out_size);
void get_soundfont_path(const char* sf_file, char* out, size_t out_size);
#endif // ASSET_PATHS_H
