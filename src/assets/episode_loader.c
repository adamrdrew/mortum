#include "assets/episode_loader.h"

#include "assets/json.h"
#include "core/log.h"

#include <stdlib.h>
#include <string.h>

static char* sv_dup(StringView s) {
	char* out = (char*)malloc(s.len + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, s.data, s.len);
	out[s.len] = '\0';
	return out;
}

static void free_strv(char** v, int n) {
	if (!v) {
		return;
	}
	for (int i = 0; i < n; i++) {
		free(v[i]);
	}
	free(v);
}

void episode_destroy(Episode* self) {
	free(self->name);
	free(self->splash);
	free_strv(self->maps, self->map_count);
	self->name = NULL;
	self->splash = NULL;
	self->maps = NULL;
	self->map_count = 0;
}

bool episode_load(Episode* out, const AssetPaths* paths, const char* episode_filename) {
	memset(out, 0, sizeof(*out));
	char* full = asset_path_join(paths, "Episodes", episode_filename);
	if (!full) {
		return false;
	}

	JsonDoc doc;
	if (!json_doc_load_file(&doc, full)) {
		free(full);
		return false;
	}
	free(full);

	if (doc.token_count < 1 || !json_token_is_object(&doc, 0)) {
		log_error("Episode JSON root must be an object");
		json_doc_destroy(&doc);
		return false;
	}

	int t_name = -1;
	int t_splash = -1;
	int t_maps = -1;
	if (!json_object_get(&doc, 0, "name", &t_name) || !json_object_get(&doc, 0, "splash", &t_splash) || !json_object_get(&doc, 0, "maps", &t_maps)) {
		log_error("Episode JSON missing required fields");
		json_doc_destroy(&doc);
		return false;
	}

	StringView sv_name;
	StringView sv_splash;
	if (!json_get_string(&doc, t_name, &sv_name) || !json_get_string(&doc, t_splash, &sv_splash)) {
		log_error("Episode name/splash must be strings");
		json_doc_destroy(&doc);
		return false;
	}
	int map_count = json_array_size(&doc, t_maps);
	if (map_count <= 0) {
		log_error("Episode maps must be a non-empty array");
		json_doc_destroy(&doc);
		return false;
	}

	out->name = sv_dup(sv_name);
	out->splash = sv_dup(sv_splash);
	out->map_count = map_count;
	out->maps = (char**)calloc((size_t)map_count, sizeof(char*));
	if (!out->name || !out->splash || !out->maps) {
		episode_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	for (int i = 0; i < map_count; i++) {
		int tok = json_array_nth(&doc, t_maps, i);
		StringView s;
		if (!json_get_string(&doc, tok, &s)) {
			log_error("Episode maps[%d] must be a string", i);
			episode_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		out->maps[i] = sv_dup(s);
		if (!out->maps[i]) {
			episode_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}

	json_doc_destroy(&doc);
	return true;
}
