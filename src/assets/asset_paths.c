// asset_paths.c

#include "assets/asset_paths.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

void get_midi_path(const char* midi_file, char* out, size_t out_size) {
    snprintf(out, out_size, "Assets/Sounds/MIDI/%s", midi_file);
}


void get_soundfont_path(const char* sf_file, char* out, size_t out_size) {
	snprintf(out, out_size, "Assets/Sounds/SoundFonts/%s", sf_file);
}

#include <stdlib.h>
#include <string.h>

static char* dup_cstr(const char* s) {
	size_t n = strlen(s);
	char* out = (char*)malloc(n + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, s, n + 1);
	return out;
}

static char* path_join3(const char* a, const char* b, const char* c) {
	size_t na = strlen(a);
	size_t nb = strlen(b);
	size_t nc = strlen(c);
	bool a_slash = (na > 0 && (a[na - 1] == '/' || a[na - 1] == '\\'));
	bool b_slash = (nb > 0 && (b[nb - 1] == '/' || b[nb - 1] == '\\'));

	// a + '/'? + b + '/'? + c + '\0'
	size_t n = na + (a_slash ? 0 : 1) + nb + (b_slash ? 0 : 1) + nc + 1;
	char* out = (char*)malloc(n);
	if (!out) {
		return NULL;
	}
	size_t off = 0;
	memcpy(out + off, a, na);
	off += na;
	if (!a_slash) {
		out[off++] = '/';
	}
	memcpy(out + off, b, nb);
	off += nb;
	if (!b_slash) {
		out[off++] = '/';
	}
	memcpy(out + off, c, nc);
	off += nc;
	out[off] = '\0';
	return out;
}

bool asset_paths_init(AssetPaths* self, const char* base_path) {
	self->assets_root = NULL;
	if (!base_path || base_path[0] == '\0') {
		// Fallback: assume CWD.
		self->assets_root = dup_cstr("Assets");
		return self->assets_root != NULL;
	}
	// SDL base path usually points to the executable dir; Assets is at repo root for now.
	// For early dev, we still default to relative Assets/ if present.
	self->assets_root = dup_cstr("Assets");
	return self->assets_root != NULL;
}

void asset_paths_destroy(AssetPaths* self) {
	free(self->assets_root);
	self->assets_root = NULL;
}

char* asset_path_join(const AssetPaths* self, const char* subdir, const char* filename) {
	return path_join3(self->assets_root ? self->assets_root : "Assets", subdir, filename);
}
