#include "assets/episode_loader.h"

#include "assets/json.h"
#include "core/path_safety.h"
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
	free_strv(self->enter_scenes, self->enter_scene_count);
	free_strv(self->exit_scenes, self->exit_scene_count);
	free_strv(self->maps, self->map_count);
	self->name = NULL;
	self->splash = NULL;
	self->enter_scenes = NULL;
	self->enter_scene_count = 0;
	self->exit_scenes = NULL;
	self->exit_scene_count = 0;
	self->maps = NULL;
	self->map_count = 0;
}

static bool parse_string_array(
	const JsonDoc* doc,
	int t_arr,
	const char* what,
	bool validate_safe_relpath,
	char*** out_v,
	int* out_n
) {
	if (!doc || !out_v || !out_n || t_arr < 0 || t_arr >= doc->token_count) {
		return false;
	}
	if (!json_token_is_array(doc, t_arr)) {
		log_error("Episode %s must be an array", what);
		return false;
	}
	int n = json_array_size(doc, t_arr);
	if (n <= 0) {
		*out_v = NULL;
		*out_n = 0;
		return true;
	}
	char** v = (char**)calloc((size_t)n, sizeof(char*));
	if (!v) {
		return false;
	}
	for (int i = 0; i < n; i++) {
		int tok = json_array_nth(doc, t_arr, i);
		StringView sv;
		if (!json_get_string(doc, tok, &sv) || sv.len <= 0) {
			log_error("Episode %s[%d] must be a non-empty string", what, i);
			free_strv(v, n);
			return false;
		}
		v[i] = sv_dup(sv);
		if (!v[i]) {
			free_strv(v, n);
			return false;
		}
		if (validate_safe_relpath && !name_is_safe_relpath(v[i])) {
			log_error("Episode %s[%d] must be a safe relative path (no '..', no backslashes): %s", what, i, v[i]);
			free_strv(v, n);
			return false;
		}
	}
	*out_v = v;
	*out_n = n;
	return true;
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
	if (!json_object_get(&doc, 0, "name", &t_name) || !json_object_get(&doc, 0, "splash", &t_splash)) {
		log_error("Episode JSON missing required fields");
		json_doc_destroy(&doc);
		return false;
	}
	(void)json_object_get(&doc, 0, "maps", &t_maps);

	StringView sv_name;
	StringView sv_splash;
	if (!json_get_string(&doc, t_name, &sv_name) || !json_get_string(&doc, t_splash, &sv_splash)) {
		log_error("Episode name/splash must be strings");
		json_doc_destroy(&doc);
		return false;
	}
	out->name = sv_dup(sv_name);
	out->splash = sv_dup(sv_splash);
	if (!out->name || !out->splash) {
		episode_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	// maps (optional; may be empty)
	out->maps = NULL;
	out->map_count = 0;
	if (t_maps != -1) {
		if (!json_token_is_array(&doc, t_maps)) {
			log_error("Episode maps must be an array of strings (may be empty)");
			episode_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		int map_count = json_array_size(&doc, t_maps);
		if (map_count < 0) {
			map_count = 0;
		}
		out->map_count = map_count;
		if (map_count > 0) {
			out->maps = (char**)calloc((size_t)map_count, sizeof(char*));
			if (!out->maps) {
				episode_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			for (int i = 0; i < map_count; i++) {
				int tok = json_array_nth(&doc, t_maps, i);
				StringView s;
				if (!json_get_string(&doc, tok, &s) || s.len <= 0) {
					log_error("Episode maps[%d] must be a non-empty string", i);
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
		}
	}

	// scenes (optional)
	out->enter_scenes = NULL;
	out->enter_scene_count = 0;
	out->exit_scenes = NULL;
	out->exit_scene_count = 0;
	int t_scenes = -1;
	if (json_object_get(&doc, 0, "scenes", &t_scenes) && t_scenes != -1) {
		if (!json_token_is_object(&doc, t_scenes)) {
			log_error("Episode scenes must be an object");
			episode_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		int t_enter = -1;
		int t_exit = -1;
		(void)json_object_get(&doc, t_scenes, "enter", &t_enter);
		(void)json_object_get(&doc, t_scenes, "exit", &t_exit);
		if (t_enter != -1) {
			if (!parse_string_array(&doc, t_enter, "scenes.enter", true, &out->enter_scenes, &out->enter_scene_count)) {
				episode_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		}
		if (t_exit != -1) {
			if (!parse_string_array(&doc, t_exit, "scenes.exit", true, &out->exit_scenes, &out->exit_scene_count)) {
				episode_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		}
	}

	json_doc_destroy(&doc);
	return true;
}
