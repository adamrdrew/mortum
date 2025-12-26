#include "assets/scene_loader.h"

#include "assets/json.h"

#include "core/path_safety.h"

#include "core/log.h"

#include <SDL.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void scene_zero(Scene* s) {
	memset(s, 0, sizeof(*s));
	s->end.scancode = -1;
}

void scene_destroy(Scene* self) {
	if (!self) {
		return;
	}
	free(self->background_png);
	free(self->music.midi_file);
	free(self->music.soundfont_file);
	free(self->sfx.enter_wav);
	free(self->sfx.exit_wav);
	free(self->text.text);
	free(self->text.font_file);
	scene_zero(self);
}

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

static bool ends_with_ci(const char* s, const char* suffix) {
	if (!s || !suffix) {
		return false;
	}
	size_t ns = strlen(s);
	size_t nf = strlen(suffix);
	if (nf > ns) {
		return false;
	}
	const char* tail = s + (ns - nf);
	for (size_t i = 0; i < nf; i++) {
		char a = (char)tolower((unsigned char)tail[i]);
		char b = (char)tolower((unsigned char)suffix[i]);
		if (a != b) {
			return false;
		}
	}
	return true;
}

static bool json_get_bool_local(const JsonDoc* doc, int tok, bool* out) {
	if (!doc || !out || tok < 0 || tok >= doc->token_count) {
		return false;
	}
	StringView sv = json_token_sv(doc, tok);
	if (sv.len == 4 && strncmp(sv.data, "true", 4) == 0) {
		*out = true;
		return true;
	}
	if (sv.len == 5 && strncmp(sv.data, "false", 5) == 0) {
		*out = false;
		return true;
	}
	return false;
}

static char* dup_sv(StringView sv) {
	char* out = (char*)malloc(sv.len + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, sv.data, sv.len);
	out[sv.len] = '\0';
	return out;
}

static bool parse_rgba8_array(const JsonDoc* doc, int tok, SceneRGBA8* out) {
	if (!doc || !out) {
		return false;
	}
	if (!json_token_is_array(doc, tok)) {
		return false;
	}
	int n = json_array_size(doc, tok);
	if (n != 3 && n != 4) {
		return false;
	}
	int t0 = json_array_nth(doc, tok, 0);
	int t1 = json_array_nth(doc, tok, 1);
	int t2 = json_array_nth(doc, tok, 2);
	int t3 = (n == 4) ? json_array_nth(doc, tok, 3) : -1;
	int r = 0, g = 0, b = 0, a = 255;
	if (!json_get_int(doc, t0, &r) || !json_get_int(doc, t1, &g) || !json_get_int(doc, t2, &b)) {
		return false;
	}
	if (t3 != -1 && !json_get_int(doc, t3, &a)) {
		return false;
	}
	if ((unsigned)r > 255u || (unsigned)g > 255u || (unsigned)b > 255u || (unsigned)a > 255u) {
		return false;
	}
	out->r = (uint8_t)r;
	out->g = (uint8_t)g;
	out->b = (uint8_t)b;
	out->a = (uint8_t)a;
	return true;
}

static bool parse_align(const JsonDoc* doc, int tok, SceneTextAlign* out) {
	if (!doc || !out) {
		return false;
	}
	if (!json_token_is_string(doc, tok)) {
		return false;
	}
	StringView sv;
	if (!json_get_string(doc, tok, &sv)) {
		return false;
	}
	if (sv.len == 4 && strncmp(sv.data, "left", 4) == 0) {
		*out = SCENE_TEXT_ALIGN_LEFT;
		return true;
	}
	if (sv.len == 6 && strncmp(sv.data, "center", 6) == 0) {
		*out = SCENE_TEXT_ALIGN_CENTER;
		return true;
	}
	if (sv.len == 5 && strncmp(sv.data, "right", 5) == 0) {
		*out = SCENE_TEXT_ALIGN_RIGHT;
		return true;
	}
	return false;
}

static bool parse_fade(const JsonDoc* doc, int tok, SceneFade* out) {
	if (!doc || !out) {
		return false;
	}
	if (!json_token_is_object(doc, tok)) {
		return false;
	}
	int t_dur = -1;
	int t_from = -1;
	(void)json_object_get(doc, tok, "duration_ms", &t_dur);
	(void)json_object_get(doc, tok, "from_rgba", &t_from);
	if (t_dur == -1 || t_from == -1) {
		return false;
	}
	int dur = 0;
	if (!json_get_int(doc, t_dur, &dur) || dur < 0) {
		return false;
	}
	SceneRGBA8 from = (SceneRGBA8){0, 0, 0, 255};
	if (!parse_rgba8_array(doc, t_from, &from)) {
		return false;
	}
	out->enabled = (dur > 0);
	out->duration_ms = dur;
	out->from = from;
	return true;
}

static bool validate_end_conditions(const Scene* s) {
	if (!s) {
		return false;
	}
	bool has_timeout = s->end.timeout_ms > 0;
	bool has_key = s->end.any_key || s->end.scancode >= 0;
	return has_timeout || has_key;
}

bool scene_load(Scene* out, const AssetPaths* paths, const char* scene_file) {
	if (!out || !paths || !scene_file || scene_file[0] == '\0') {
		return false;
	}
	scene_zero(out);

	if (!name_is_safe_relpath(scene_file) || !ends_with_ci(scene_file, ".json")) {
		log_error("Scene filename must be a safe relative .json path under Assets/Scenes: %s", scene_file);
		return false;
	}

	char* full = asset_path_join(paths, "Scenes", scene_file);
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
		log_error("Scene JSON root must be an object");
		json_doc_destroy(&doc);
		return false;
	}

	// background_png (required)
	int t_bg = -1;
	if (!json_object_get(&doc, 0, "background_png", &t_bg) || t_bg == -1 || !json_token_is_string(&doc, t_bg)) {
		log_error("Scene missing required string: background_png");
		json_doc_destroy(&doc);
		return false;
	}
	StringView sv_bg;
	if (!json_get_string(&doc, t_bg, &sv_bg)) {
		json_doc_destroy(&doc);
		return false;
	}
	out->background_png = dup_sv(sv_bg);
	if (!out->background_png) {
		json_doc_destroy(&doc);
		return false;
	}
	if (!name_is_safe_relpath(out->background_png) || !ends_with_ci(out->background_png, ".png")) {
		log_error("Scene background_png must be a safe relative .png path under Assets/Images: %s", out->background_png);
		scene_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	char* bg_full = asset_path_join(paths, "Images", out->background_png);
	if (!bg_full || !file_exists(bg_full)) {
		log_error("Scene background image not found: %s", bg_full ? bg_full : "(alloc failed)");
		free(bg_full);
		scene_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	free(bg_full);

	// Optional music
	int t_music = -1;
	if (json_object_get(&doc, 0, "music", &t_music) && t_music != -1) {
		if (!json_token_is_object(&doc, t_music)) {
			log_error("Scene music must be an object");
			scene_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		int t_midi = -1;
		int t_sf = -1;
		(void)json_object_get(&doc, t_music, "midi", &t_midi);
		(void)json_object_get(&doc, t_music, "soundfont", &t_sf);
		if (t_midi != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_midi, &sv)) {
				log_error("Scene music.midi must be a string");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->music.midi_file = dup_sv(sv);
			if (!out->music.midi_file) {
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_relpath(out->music.midi_file) || !(ends_with_ci(out->music.midi_file, ".mid") || ends_with_ci(out->music.midi_file, ".midi"))) {
				log_error("Scene music.midi must be a safe relative .mid/.midi filename under Assets/Sounds/MIDI: %s", out->music.midi_file);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			char* midi_full = asset_path_join(paths, "Sounds/MIDI", out->music.midi_file);
			if (!midi_full || !file_exists(midi_full)) {
				log_error("Scene MIDI file not found: %s", midi_full ? midi_full : "(alloc failed)");
				free(midi_full);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			free(midi_full);
		}
		if (t_sf != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_sf, &sv)) {
				log_error("Scene music.soundfont must be a string");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->music.soundfont_file = dup_sv(sv);
			if (!out->music.soundfont_file) {
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_relpath(out->music.soundfont_file) || !ends_with_ci(out->music.soundfont_file, ".sf2")) {
				log_error("Scene music.soundfont must be a safe relative .sf2 filename under Assets/Sounds/SoundFonts: %s", out->music.soundfont_file);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			char* sf_full = asset_path_join(paths, "Sounds/SoundFonts", out->music.soundfont_file);
			if (!sf_full || !file_exists(sf_full)) {
				log_error("Scene soundfont not found: %s", sf_full ? sf_full : "(alloc failed)");
				free(sf_full);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			free(sf_full);
		} else if (out->music.midi_file) {
			out->music.soundfont_file = strdup("hl4mgm.sf2");
		}
	}

	// Optional text
	int t_text = -1;
	if (json_object_get(&doc, 0, "text", &t_text) && t_text != -1) {
		if (!json_token_is_object(&doc, t_text)) {
			log_error("Scene text must be an object");
			scene_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		out->text.enabled = true;
		out->text.opacity = 1.0f;
		out->text.align = SCENE_TEXT_ALIGN_CENTER;
		out->text.size_px = 16;
		out->text.atlas_size = 512;
		out->text.color = (SceneRGBA8){255, 255, 255, 255};
		out->text.scroll = false;
		out->text.scroll_speed_px_s = 30.0f;
		out->text.x_px = -1;
		out->text.y_px = -1;

		int t_value = -1;
		if (json_object_get(&doc, t_text, "value", &t_value) && t_value != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_value, &sv)) {
				log_error("Scene text.value must be a string");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->text.text = dup_sv(sv);
		}

		int t_font = -1;
		if (json_object_get(&doc, t_text, "font", &t_font) && t_font != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_font, &sv)) {
				log_error("Scene text.font must be a string");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->text.font_file = dup_sv(sv);
			if (!out->text.font_file) {
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_relpath(out->text.font_file)) {
				log_error("Scene text.font must be a safe filename under Assets/Fonts: %s", out->text.font_file);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		}

		int t_size = -1;
		if (json_object_get(&doc, t_text, "size_px", &t_size) && t_size != -1) {
			int v = 0;
			if (!json_get_int(&doc, t_size, &v) || v < 6 || v > 96) {
				log_error("Scene text.size_px must be int in [6..96]");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->text.size_px = v;
		}
		int t_atlas = -1;
		if (json_object_get(&doc, t_text, "atlas_size", &t_atlas) && t_atlas != -1) {
			int v = 0;
			if (!json_get_int(&doc, t_atlas, &v) || v < 128 || v > 4096) {
				log_error("Scene text.atlas_size must be int in [128..4096]");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->text.atlas_size = v;
		}

		int t_align = -1;
		if (json_object_get(&doc, t_text, "align", &t_align) && t_align != -1) {
			if (!parse_align(&doc, t_align, &out->text.align)) {
				log_error("Scene text.align must be one of: left|center|right");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		}

		int t_color = -1;
		if (json_object_get(&doc, t_text, "color_rgba", &t_color) && t_color != -1) {
			if (!parse_rgba8_array(&doc, t_color, &out->text.color)) {
				log_error("Scene text.color_rgba must be an array: [r,g,b] or [r,g,b,a]");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		}

		int t_op = -1;
		if (json_object_get(&doc, t_text, "opacity", &t_op) && t_op != -1) {
			double d = 0.0;
			if (!json_get_double(&doc, t_op, &d)) {
				log_error("Scene text.opacity must be a number");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (d < 0.0) d = 0.0;
			if (d > 1.0) d = 1.0;
			out->text.opacity = (float)d;
		}

		int t_x = -1;
		int t_y = -1;
		if (json_object_get(&doc, t_text, "x_px", &t_x) && t_x != -1) {
			(void)json_get_int(&doc, t_x, &out->text.x_px);
		}
		if (json_object_get(&doc, t_text, "y_px", &t_y) && t_y != -1) {
			(void)json_get_int(&doc, t_y, &out->text.y_px);
		}

		int t_scroll = -1;
		if (json_object_get(&doc, t_text, "scroll", &t_scroll) && t_scroll != -1) {
			bool b = false;
			if (!json_get_bool_local(&doc, t_scroll, &b)) {
				log_error("Scene text.scroll must be a boolean");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->text.scroll = b;
		}
		int t_speed = -1;
		if (json_object_get(&doc, t_text, "scroll_speed_px_s", &t_speed) && t_speed != -1) {
			double d = 0.0;
			if (!json_get_double(&doc, t_speed, &d)) {
				log_error("Scene text.scroll_speed_px_s must be a number");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->text.scroll_speed_px_s = (float)d;
		}

		// Validate minimal text object
		if (!out->text.text || out->text.text[0] == '\0') {
			// treat as disabled if empty
			out->text.enabled = false;
		}
		if (out->text.enabled && (!out->text.font_file || out->text.font_file[0] == '\0')) {
			// default to engine UI font if not specified
			out->text.font_file = strdup("ProggyClean.ttf");
		}
	}

	// Optional sfx
	int t_sfx = -1;
	if (json_object_get(&doc, 0, "sfx", &t_sfx) && t_sfx != -1) {
		if (!json_token_is_object(&doc, t_sfx)) {
			log_error("Scene sfx must be an object");
			scene_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		int t_enter = -1;
		int t_exit = -1;
		(void)json_object_get(&doc, t_sfx, "enter_wav", &t_enter);
		(void)json_object_get(&doc, t_sfx, "exit_wav", &t_exit);
		if (t_enter != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_enter, &sv)) {
				log_error("Scene sfx.enter_wav must be a string");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->sfx.enter_wav = dup_sv(sv);
			if (!out->sfx.enter_wav) {
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_relpath(out->sfx.enter_wav) || !ends_with_ci(out->sfx.enter_wav, ".wav")) {
				log_error("Scene sfx.enter_wav must be a safe relative .wav filename under Assets/Sounds/Effects: %s", out->sfx.enter_wav);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			char* p = asset_path_join(paths, "Sounds/Effects", out->sfx.enter_wav);
			if (!p || !file_exists(p)) {
				log_error("Scene enter WAV not found: %s", p ? p : "(alloc failed)");
				free(p);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			free(p);
		}
		if (t_exit != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_exit, &sv)) {
				log_error("Scene sfx.exit_wav must be a string");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->sfx.exit_wav = dup_sv(sv);
			if (!out->sfx.exit_wav) {
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_relpath(out->sfx.exit_wav) || !ends_with_ci(out->sfx.exit_wav, ".wav")) {
				log_error("Scene sfx.exit_wav must be a safe relative .wav filename under Assets/Sounds/Effects: %s", out->sfx.exit_wav);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			char* p = asset_path_join(paths, "Sounds/Effects", out->sfx.exit_wav);
			if (!p || !file_exists(p)) {
				log_error("Scene exit WAV not found: %s", p ? p : "(alloc failed)");
				free(p);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			free(p);
		}
	}

	// End conditions (required)
	out->end.timeout_ms = 0;
	out->end.any_key = false;
	out->end.scancode = -1;
	int t_end = -1;
	if (json_object_get(&doc, 0, "end", &t_end) && t_end != -1) {
		if (!json_token_is_object(&doc, t_end)) {
			log_error("Scene end must be an object");
			scene_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		int t_timeout = -1;
		if (json_object_get(&doc, t_end, "timeout_ms", &t_timeout) && t_timeout != -1) {
			int v = 0;
			if (!json_get_int(&doc, t_timeout, &v) || v < 0) {
				log_error("Scene end.timeout_ms must be a non-negative int");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->end.timeout_ms = v;
		}

		int t_any = -1;
		if (json_object_get(&doc, t_end, "any_key", &t_any) && t_any != -1) {
			bool b = false;
			if (!json_get_bool_local(&doc, t_any, &b)) {
				log_error("Scene end.any_key must be a boolean");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->end.any_key = b;
		}

		int t_key = -1;
		if (json_object_get(&doc, t_end, "key", &t_key) && t_key != -1) {
			StringView sv;
			if (!json_get_string(&doc, t_key, &sv)) {
				log_error("Scene end.key must be a string (SDL scancode name)");
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			char tmp[64];
			size_t n = sv.len;
			if (n >= sizeof(tmp)) {
				n = sizeof(tmp) - 1;
			}
			memcpy(tmp, sv.data, n);
			tmp[n] = '\0';
			SDL_Scancode sc = SDL_GetScancodeFromName(tmp);
			if (sc == SDL_SCANCODE_UNKNOWN) {
				log_error("Scene end.key unknown scancode name: %s", tmp);
				scene_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			out->end.scancode = (int)sc;
		}
	} else {
		log_error("Scene missing required object: end");
		scene_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	if (!validate_end_conditions(out)) {
		log_error("Scene must define at least one end condition: end.timeout_ms and/or end.any_key/end.key");
		scene_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	// Optional fades
	out->fade_in.enabled = false;
	out->fade_out.enabled = false;
	int t_fade_in = -1;
	if (json_object_get(&doc, 0, "fade_in", &t_fade_in) && t_fade_in != -1) {
		if (!parse_fade(&doc, t_fade_in, &out->fade_in)) {
			log_error("Scene fade_in must be { duration_ms, from_rgba }");
			scene_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}
	int t_fade_out = -1;
	if (json_object_get(&doc, 0, "fade_out", &t_fade_out) && t_fade_out != -1) {
		if (!parse_fade(&doc, t_fade_out, &out->fade_out)) {
			log_error("Scene fade_out must be { duration_ms, from_rgba }");
			scene_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}

	json_doc_destroy(&doc);
	return true;
}
