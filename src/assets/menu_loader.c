#include "assets/menu_loader.h"

#include "assets/json.h"
#include "core/log.h"
#include "core/path_safety.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void menu_theme_zero(MenuTheme* t) {
	if (!t) {
		return;
	}
	memset(t, 0, sizeof(*t));
	t->cursor_render_size_px = 32;
	t->text_size_px = 18;
	t->text_color = (MenuRGBA8){255, 255, 255, 255};
}

static void menu_action_zero(MenuAction* a) {
	if (!a) {
		return;
	}
	memset(a, 0, sizeof(*a));
	a->kind = MENU_ACTION_NONE;
}

static void menu_item_zero(MenuItem* it) {
	if (!it) {
		return;
	}
	memset(it, 0, sizeof(*it));
	menu_action_zero(&it->action);
}

static void menu_view_zero(MenuView* v) {
	if (!v) {
		return;
	}
	memset(v, 0, sizeof(*v));
}

void menu_asset_destroy(MenuAsset* self) {
	if (!self) {
		return;
	}
	free(self->name);
	self->name = NULL;

	MenuTheme* t = &self->theme;
	free(t->background_png);
	free(t->cursor_png);
	free(t->font_ttf);
	free(t->music_midi);
	free(t->sfx.on_move_wav);
	free(t->sfx.on_select_wav);
	free(t->sfx.on_back_wav);
	menu_theme_zero(t);

	if (self->views) {
		for (int vi = 0; vi < self->view_count; vi++) {
			MenuView* v = &self->views[vi];
			if (v->items) {
				for (int ii = 0; ii < v->item_count; ii++) {
					MenuItem* it = &v->items[ii];
					free(it->label);
					it->label = NULL;
					MenuAction* a = &it->action;
					free(a->command);
					a->command = NULL;
					if (a->args) {
						for (int ai = 0; ai < a->arg_count; ai++) {
							free(a->args[ai]);
						}
						free(a->args);
					}
					a->args = NULL;
					a->arg_count = 0;
					free(a->submenu_id);
					a->submenu_id = NULL;
					a->kind = MENU_ACTION_NONE;
				}
				free(v->items);
				v->items = NULL;
				v->item_count = 0;
			}
			free(v->id);
			free(v->title);
			v->id = NULL;
			v->title = NULL;
			menu_view_zero(v);
		}
		free(self->views);
	}
	self->views = NULL;
	self->view_count = 0;
	self->root_view_index = -1;
	memset(self, 0, sizeof(*self));
	self->root_view_index = -1;
	menu_theme_zero(&self->theme);
}

int menu_asset_find_view(const MenuAsset* self, const char* id) {
	if (!self || !self->views || self->view_count <= 0 || !id || !id[0]) {
		return -1;
	}
	for (int i = 0; i < self->view_count; i++) {
		const MenuView* v = &self->views[i];
		if (v->id && strcmp(v->id, id) == 0) {
			return i;
		}
	}
	return -1;
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

static char* sv_dup(StringView s) {
	char* out = (char*)malloc(s.len + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, s.data, s.len);
	out[s.len] = '\0';
	return out;
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

// Stricter than name_is_safe_relpath: disallows any path separators.
static bool name_is_safe_filename(const char* name) {
	if (!name || !name[0]) {
		return false;
	}
	if (strstr(name, "..") != NULL) {
		return false;
	}
	for (const char* p = name; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '/' || c == '\\') {
			return false;
		}
		if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
			return false;
		}
	}
	return true;
}

static bool parse_hex_byte(char a, char b, uint8_t* out) {
	int hi = isdigit((unsigned char)a) ? (a - '0') : (isxdigit((unsigned char)a) ? (10 + (tolower((unsigned char)a) - 'a')) : -1);
	int lo = isdigit((unsigned char)b) ? (b - '0') : (isxdigit((unsigned char)b) ? (10 + (tolower((unsigned char)b) - 'a')) : -1);
	if (hi < 0 || hi > 15 || lo < 0 || lo > 15) {
		return false;
	}
	*out = (uint8_t)((hi << 4) | lo);
	return true;
}

static bool parse_color_hex(StringView sv, MenuRGBA8* out) {
	if (!out) {
		return false;
	}
	// Accept "#RRGGBB" only for MVP.
	if (sv.len != 7 || sv.data[0] != '#') {
		return false;
	}
	uint8_t r = 0, g = 0, b = 0;
	if (!parse_hex_byte(sv.data[1], sv.data[2], &r)) return false;
	if (!parse_hex_byte(sv.data[3], sv.data[4], &g)) return false;
	if (!parse_hex_byte(sv.data[5], sv.data[6], &b)) return false;
	*out = (MenuRGBA8){r, g, b, 255};
	return true;
}

// For a container token at `tok`, returns index of next token after its subtree.
static int tok_next_local(const JsonDoc* doc, int tok) {
	if (!doc || tok < 0 || tok >= doc->token_count) {
		return tok + 1;
	}
	const jsmntok_t* t = &doc->tokens[tok];
	if (t->type == JSMN_STRING || t->type == JSMN_PRIMITIVE) {
		return tok + 1;
	}
	int i = tok + 1;
	for (int n = 0; n < t->size; n++) {
		i = tok_next_local(doc, i);
	}
	return i;
}

static bool parse_action(MenuAction* out, const JsonDoc* doc, int t_action) {
	if (!out || !doc) {
		return false;
	}
	menu_action_zero(out);
	if (!json_token_is_object(doc, t_action)) {
		return false;
	}

	int t_kind = -1;
	if (!json_object_get(doc, t_action, "kind", &t_kind) || t_kind < 0 || !json_token_is_string(doc, t_kind)) {
		return false;
	}
	StringView sv_kind;
	if (!json_get_string(doc, t_kind, &sv_kind) || sv_kind.len <= 0) {
		return false;
	}

	if (sv_kind.len == 7 && strncmp(sv_kind.data, "command", 7) == 0) {
		out->kind = MENU_ACTION_COMMAND;
		int t_cmd = -1;
		if (!json_object_get(doc, t_action, "command", &t_cmd) || t_cmd < 0 || !json_token_is_string(doc, t_cmd)) {
			return false;
		}
		StringView sv_cmd;
		if (!json_get_string(doc, t_cmd, &sv_cmd) || sv_cmd.len <= 0) {
			return false;
		}
		out->command = sv_dup(sv_cmd);
		if (!out->command) {
			return false;
		}
		int t_args = -1;
		(void)json_object_get(doc, t_action, "args", &t_args);
		if (t_args >= 0) {
			if (!json_token_is_array(doc, t_args)) {
				return false;
			}
			int n = json_array_size(doc, t_args);
			if (n < 0) n = 0;
			out->arg_count = n;
			if (n > 0) {
				out->args = (char**)calloc((size_t)n, sizeof(char*));
				if (!out->args) {
					return false;
				}
				for (int i = 0; i < n; i++) {
					int t_a = json_array_nth(doc, t_args, i);
					if (t_a < 0 || !json_token_is_string(doc, t_a)) {
						return false;
					}
					StringView sv_a;
					if (!json_get_string(doc, t_a, &sv_a)) {
						return false;
					}
					out->args[i] = sv_dup(sv_a);
					if (!out->args[i]) {
						return false;
					}
				}
			}
		}
		return true;
	}

	if (sv_kind.len == 5 && strncmp(sv_kind.data, "close", 5) == 0) {
		out->kind = MENU_ACTION_CLOSE;
		return true;
	}

	if (sv_kind.len == 7 && strncmp(sv_kind.data, "submenu", 7) == 0) {
		out->kind = MENU_ACTION_SUBMENU;
		int t_view = -1;
		if (!json_object_get(doc, t_action, "view", &t_view) || t_view < 0 || !json_token_is_string(doc, t_view)) {
			return false;
		}
		StringView sv_view;
		if (!json_get_string(doc, t_view, &sv_view) || sv_view.len <= 0) {
			return false;
		}
		out->submenu_id = sv_dup(sv_view);
		return out->submenu_id != NULL;
	}

	// Close the menu screen.
	if ((sv_kind.len == 5 && strncmp(sv_kind.data, "close", 5) == 0) || (sv_kind.len == 4 && strncmp(sv_kind.data, "none", 4) == 0)) {
		out->kind = MENU_ACTION_NONE;
		return true;
	}

	return false;
}

static bool parse_view(MenuView* out, const JsonDoc* doc, int t_view_obj, StringView view_id_key) {
	if (!out || !doc) {
		return false;
	}
	menu_view_zero(out);
	out->id = sv_dup(view_id_key);
	if (!out->id) {
		return false;
	}
	if (!json_token_is_object(doc, t_view_obj)) {
		return false;
	}

	// Disallow theme overrides at view level.
	int t_theme = -1;
	if (json_object_get(doc, t_view_obj, "theme", &t_theme) && t_theme >= 0) {
		log_error("Menu view '%s' must not define 'theme' (theme is root-only)", out->id);
		return false;
	}

	int t_title = -1;
	(void)json_object_get(doc, t_view_obj, "title", &t_title);
	if (t_title >= 0) {
		if (!json_token_is_string(doc, t_title)) {
			return false;
		}
		StringView sv_title;
		if (!json_get_string(doc, t_title, &sv_title)) {
			return false;
		}
		out->title = sv_dup(sv_title);
		if (!out->title) {
			return false;
		}
	}

	int t_items = -1;
	if (!json_object_get(doc, t_view_obj, "items", &t_items) || t_items < 0 || !json_token_is_array(doc, t_items)) {
		log_error("Menu view '%s' missing required array: items", out->id);
		return false;
	}
	int n = json_array_size(doc, t_items);
	if (n < 0) n = 0;
	out->item_count = n;
	out->items = NULL;
	if (n > 0) {
		out->items = (MenuItem*)calloc((size_t)n, sizeof(MenuItem));
		if (!out->items) {
			return false;
		}
	}

	for (int i = 0; i < n; i++) {
		MenuItem* it = &out->items[i];
		menu_item_zero(it);
		int t_item = json_array_nth(doc, t_items, i);
		if (t_item < 0 || !json_token_is_object(doc, t_item)) {
			log_error("Menu view '%s' items[%d] must be an object", out->id, i);
			return false;
		}
		int t_label = -1;
		int t_action = -1;
		if (!json_object_get(doc, t_item, "label", &t_label) || t_label < 0 || !json_token_is_string(doc, t_label)) {
			log_error("Menu view '%s' items[%d] missing required string: label", out->id, i);
			return false;
		}
		if (!json_object_get(doc, t_item, "action", &t_action) || t_action < 0) {
			log_error("Menu view '%s' items[%d] missing required object: action", out->id, i);
			return false;
		}
		StringView sv_label;
		if (!json_get_string(doc, t_label, &sv_label) || sv_label.len <= 0) {
			return false;
		}
		it->label = sv_dup(sv_label);
		if (!it->label) {
			return false;
		}
		if (!parse_action(&it->action, doc, t_action)) {
			log_error("Menu view '%s' items[%d] has invalid action", out->id, i);
			return false;
		}
	}

	return true;
}

bool menu_load(MenuAsset* out, const AssetPaths* paths, const char* menu_file) {
	if (!out) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	out->root_view_index = -1;
	menu_theme_zero(&out->theme);

	if (!paths || !menu_file || menu_file[0] == '\0') {
		log_error("Menu load: missing filename");
		return false;
	}
	if (!name_is_safe_relpath(menu_file) || !ends_with_ci(menu_file, ".json")) {
		log_error("Menu filename must be a safe relative .json path under Assets/Menus: %s", menu_file);
		return false;
	}

	char* full = asset_path_join(paths, "Menus", menu_file);
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
		log_error("Menu JSON root must be an object");
		json_doc_destroy(&doc);
		return false;
	}

	int t_name = -1;
	int t_theme = -1;
	int t_views = -1;
	if (!json_object_get(&doc, 0, "name", &t_name) || !json_object_get(&doc, 0, "theme", &t_theme) || !json_object_get(&doc, 0, "views", &t_views)) {
		log_error("Menu JSON missing required fields: name, theme, views");
		json_doc_destroy(&doc);
		return false;
	}
	StringView sv_name;
	if (!json_get_string(&doc, t_name, &sv_name) || sv_name.len <= 0) {
		log_error("Menu name must be a non-empty string");
		json_doc_destroy(&doc);
		return false;
	}
	out->name = sv_dup(sv_name);
	if (!out->name) {
		json_doc_destroy(&doc);
		return false;
	}

	if (!json_token_is_object(&doc, t_theme)) {
		log_error("Menu theme must be an object");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	// Theme fields.
	int t_bg = -1;
	int t_cursor = -1;
	int t_cursor_render_size = -1;
	int t_font = -1;
	int t_music = -1;
	int t_text_size = -1;
	int t_text_color = -1;
	int t_sfx = -1;
	if (!json_object_get(&doc, t_theme, "background", &t_bg) || !json_object_get(&doc, t_theme, "font", &t_font) ||
		!json_object_get(&doc, t_theme, "text_size", &t_text_size) || !json_object_get(&doc, t_theme, "text_color", &t_text_color)) {
		log_error("Menu theme missing required fields: background, font, text_size, text_color");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	(void)json_object_get(&doc, t_theme, "cursor", &t_cursor);
	(void)json_object_get(&doc, t_theme, "cursor_render_size", &t_cursor_render_size);
	(void)json_object_get(&doc, t_theme, "music", &t_music);
	(void)json_object_get(&doc, t_theme, "sfx", &t_sfx);

	StringView sv_bg;
	StringView sv_cursor;
	StringView sv_font;
	StringView sv_color;
	int text_size = 0;
	if (!json_get_string(&doc, t_bg, &sv_bg) || sv_bg.len <= 0) {
		log_error("Menu theme.background must be a non-empty string");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	sv_cursor = (StringView){0};
	if (t_cursor >= 0) {
		if (!json_get_string(&doc, t_cursor, &sv_cursor)) {
			log_error("Menu theme.cursor must be a string if present");
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}
	if (!json_get_string(&doc, t_font, &sv_font) || sv_font.len <= 0) {
		log_error("Menu theme.font must be a non-empty string");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	if (!json_get_int(&doc, t_text_size, &text_size) || text_size <= 0 || text_size > 128) {
		log_error("Menu theme.text_size must be a positive integer <= 128");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	if (!json_get_string(&doc, t_text_color, &sv_color) || sv_color.len <= 0) {
		log_error("Menu theme.text_color must be a string like '#RRGGBB'");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	MenuRGBA8 color;
	if (!parse_color_hex(sv_color, &color)) {
		log_error("Menu theme.text_color must be '#RRGGBB'");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	int cursor_render_size = out->theme.cursor_render_size_px;
	if (t_cursor_render_size >= 0) {
		if (!json_get_int(&doc, t_cursor_render_size, &cursor_render_size) || cursor_render_size <= 0 || cursor_render_size > 512) {
			log_error("Menu theme.cursor_render_size must be a positive integer <= 512");
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}

	out->theme.text_size_px = text_size;
	out->theme.text_color = color;
	out->theme.cursor_render_size_px = cursor_render_size;
	out->theme.background_png = sv_dup(sv_bg);
	out->theme.cursor_png = (sv_cursor.len > 0) ? sv_dup(sv_cursor) : NULL;
	out->theme.font_ttf = sv_dup(sv_font);
	if (!out->theme.background_png || !out->theme.font_ttf) {
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	if (!name_is_safe_filename(out->theme.background_png) || !ends_with_ci(out->theme.background_png, ".png")) {
		log_error("Menu theme.background must be a safe .png filename (no slashes) under Assets/Images/Menus/Backgrounds: %s", out->theme.background_png);
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}
	if (out->theme.cursor_png && out->theme.cursor_png[0] != '\0') {
		if (!name_is_safe_filename(out->theme.cursor_png) || !ends_with_ci(out->theme.cursor_png, ".png")) {
			log_error("Menu theme.cursor must be a safe .png filename (no slashes) under Assets/Images/Menus/Cursors: %s", out->theme.cursor_png);
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}
	if (!name_is_safe_filename(out->theme.font_ttf) || !ends_with_ci(out->theme.font_ttf, ".ttf")) {
		log_error("Menu theme.font must be a safe .ttf filename (no slashes) under Assets/Fonts: %s", out->theme.font_ttf);
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	// Validate asset existence eagerly (validator uses this too).
	{
		char* bg_full = asset_path_join(paths, "Images/Menus/Backgrounds", out->theme.background_png);
		if (!bg_full || !file_exists(bg_full)) {
			log_error("Menu background image not found: %s", bg_full ? bg_full : "(alloc failed)");
			free(bg_full);
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		free(bg_full);
		if (out->theme.cursor_png && out->theme.cursor_png[0] != '\0') {
			char* cur_full = asset_path_join(paths, "Images/Menus/Cursors", out->theme.cursor_png);
			if (!cur_full || !file_exists(cur_full)) {
				log_warn("Menu cursor image not found (falling back): %s", cur_full ? cur_full : "(alloc failed)");
				free(cur_full);
				free(out->theme.cursor_png);
				out->theme.cursor_png = NULL;
			} else {
				free(cur_full);
			}
		}
		char* font_full = asset_path_join(paths, "Fonts", out->theme.font_ttf);
		if (!font_full || !file_exists(font_full)) {
			log_error("Menu font not found: %s", font_full ? font_full : "(alloc failed)");
			free(font_full);
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		free(font_full);
	}

	if (t_music >= 0) {
		if (!json_token_is_string(&doc, t_music)) {
			log_error("Menu theme.music must be a string");
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		StringView sv_music;
		if (!json_get_string(&doc, t_music, &sv_music)) {
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		if (sv_music.len > 0) {
			out->theme.music_midi = sv_dup(sv_music);
			if (!out->theme.music_midi) {
				menu_asset_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_relpath(out->theme.music_midi) || !(ends_with_ci(out->theme.music_midi, ".mid") || ends_with_ci(out->theme.music_midi, ".midi"))) {
				log_error("Menu theme.music must be a safe relative .mid/.midi path under Assets/Sounds/MIDI: %s", out->theme.music_midi);
				menu_asset_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			char* midi_full = asset_path_join(paths, "Sounds/MIDI", out->theme.music_midi);
			if (!midi_full || !file_exists(midi_full)) {
				log_error("Menu MIDI file not found: %s", midi_full ? midi_full : "(alloc failed)");
				free(midi_full);
				menu_asset_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			free(midi_full);
		}
	}

	if (t_sfx >= 0) {
		if (!json_token_is_object(&doc, t_sfx)) {
			log_error("Menu theme.sfx must be an object");
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		int t_mv = -1, t_sel = -1, t_back = -1;
		(void)json_object_get(&doc, t_sfx, "on_move", &t_mv);
		(void)json_object_get(&doc, t_sfx, "on_select", &t_sel);
		(void)json_object_get(&doc, t_sfx, "on_back", &t_back);
		struct { int tok; char** dst; const char* key; } fields[3] = {
			{t_mv, &out->theme.sfx.on_move_wav, "on_move"},
			{t_sel, &out->theme.sfx.on_select_wav, "on_select"},
			{t_back, &out->theme.sfx.on_back_wav, "on_back"},
		};
		for (int i = 0; i < 3; i++) {
			if (fields[i].tok < 0) {
				continue;
			}
			if (!json_token_is_string(&doc, fields[i].tok)) {
				log_error("Menu theme.sfx.%s must be a string", fields[i].key);
				menu_asset_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			StringView sv;
			(void)json_get_string(&doc, fields[i].tok, &sv);
			if (sv.len <= 0) {
				continue;
			}
			*fields[i].dst = sv_dup(sv);
			if (!*fields[i].dst) {
				menu_asset_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_filename(*fields[i].dst) || !ends_with_ci(*fields[i].dst, ".wav")) {
				log_error("Menu theme.sfx.%s must be a safe .wav filename (no slashes) under Assets/Sounds/Menus: %s", fields[i].key, *fields[i].dst);
				menu_asset_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			char* full_wav = asset_path_join(paths, "Sounds/Menus", *fields[i].dst);
			if (!full_wav || !file_exists(full_wav)) {
				log_warn("Menu SFX file not found (ignoring): %s", full_wav ? full_wav : "(alloc failed)");
				free(full_wav);
				free(*fields[i].dst);
				*fields[i].dst = NULL;
				continue;
			}
			free(full_wav);
		}
	}

	if (!json_token_is_object(&doc, t_views)) {
		log_error("Menu views must be an object");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	// Iterate views object pairs.
	const jsmntok_t* views_tok = &doc.tokens[t_views];
	int pair_count = views_tok ? (views_tok->size / 2) : 0;
	out->view_count = pair_count;
	out->views = NULL;
	if (pair_count > 0) {
		out->views = (MenuView*)calloc((size_t)pair_count, sizeof(MenuView));
		if (!out->views) {
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}

	int idx = 0;
	int i = t_views + 1;
	for (int pair = 0; pair < pair_count; pair++) {
		int t_key = i;
		int t_val = i + 1;
		if (!json_token_is_string(&doc, t_key)) {
			log_error("Menu views keys must be strings");
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		StringView sv_key = json_token_sv(&doc, t_key);
		if (sv_key.len <= 0) {
			log_error("Menu views contains an empty key");
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		if (!parse_view(&out->views[idx], &doc, t_val, sv_key)) {
			menu_asset_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		idx++;
		i = tok_next_local(&doc, t_val);
	}

	out->root_view_index = menu_asset_find_view(out, "root");
	if (out->root_view_index < 0) {
		log_error("Menu must define views.root");
		menu_asset_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	// Validate submenu targets.
	for (int vi = 0; vi < out->view_count; vi++) {
		MenuView* v = &out->views[vi];
		for (int ii = 0; ii < v->item_count; ii++) {
			MenuAction* a = &v->items[ii].action;
			if (a->kind == MENU_ACTION_SUBMENU) {
				if (!a->submenu_id || a->submenu_id[0] == '\0') {
					log_error("Menu view '%s' item '%s' submenu action missing view id", v->id ? v->id : "", v->items[ii].label ? v->items[ii].label : "");
					menu_asset_destroy(out);
					json_doc_destroy(&doc);
					return false;
				}
				if (menu_asset_find_view(out, a->submenu_id) < 0) {
					log_error("Menu submenu target not found: %s", a->submenu_id);
					menu_asset_destroy(out);
					json_doc_destroy(&doc);
					return false;
				}
			}
		}
	}

	log_info_s("menu", "Menu loaded: name='%s' views=%d", out->name ? out->name : "(null)", out->view_count);
	json_doc_destroy(&doc);
	return true;
}
