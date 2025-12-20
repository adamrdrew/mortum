#include "platform/fs.h"

#include "core/log.h"

#include <SDL.h>
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

bool fs_paths_init(FsPaths* self, const char* org, const char* app) {
	self->base_path = NULL;
	self->pref_path = NULL;

	char* base = SDL_GetBasePath();
	if (!base) {
		log_error("SDL_GetBasePath failed: %s", SDL_GetError());
		return false;
	}
	self->base_path = dup_cstr(base);
	SDL_free(base);
	if (!self->base_path) {
		return false;
	}

	char* pref = SDL_GetPrefPath(org, app);
	if (!pref) {
		log_error("SDL_GetPrefPath failed: %s", SDL_GetError());
		fs_paths_destroy(self);
		return false;
	}
	self->pref_path = dup_cstr(pref);
	SDL_free(pref);
	if (!self->pref_path) {
		fs_paths_destroy(self);
		return false;
	}

	return true;
}

void fs_paths_destroy(FsPaths* self) {
	free(self->base_path);
	free(self->pref_path);
	self->base_path = NULL;
	self->pref_path = NULL;
}
