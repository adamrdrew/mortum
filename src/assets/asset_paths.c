// asset_paths.c

#include "assets/asset_paths.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static bool dir_exists(const char* path) {
	if (!path || path[0] == '\0') {
		return false;
	}
	struct stat st;
	if (stat(path, &st) != 0) {
		return false;
	}
	return S_ISDIR(st.st_mode);
}

bool asset_paths_init(AssetPaths* self, const char* base_path) {
	self->assets_root = NULL;
	if (!base_path || base_path[0] == '\0') {
		// Fallback: assume CWD.
		self->assets_root = dup_cstr("Assets");
		return self->assets_root != NULL;
	}
	// SDL base path points to the executable directory.
	// Assets root is always relative to the binary.
	// Prefer: <base_path>/Assets
	// Dev fallback (e.g., when binary is in build/): <base_path>/../Assets
	char* primary = path_join3(base_path, "Assets", "");
	if (primary && dir_exists(primary)) {
		self->assets_root = primary;
		return true;
	}
	free(primary);

	char* secondary = path_join3(base_path, "..", "Assets");
	if (secondary && dir_exists(secondary)) {
		self->assets_root = secondary;
		return true;
	}
	free(secondary);

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
