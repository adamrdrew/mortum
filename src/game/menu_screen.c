#include "game/menu_screen.h"

#include "assets/asset_paths.h"
#include "assets/image.h"
#include "assets/midi_player.h"

#include "core/log.h"

#include "game/console_commands.h"
#include "game/font.h"

#include "platform/audio.h"

#include "render/draw.h"

#include <SDL.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define MENU_STACK_MAX 16
#define MENU_CMD_MAX 512

typedef struct MenuScreen {
	Screen base;
	MenuAsset asset;
	bool invoked_from_timeline;
	ConsoleCommandContext* cmd_ctx; // not owned
	bool ignore_esc_until_released;

	// Runtime state
	int stack_depth;
	int view_stack[MENU_STACK_MAX];
	int sel_stack[MENU_STACK_MAX];

	// Theme assets
	Image bg;
	bool bg_loaded;

	Image cursor;
	bool cursor_loaded;

	FontSystem font;
	bool font_loaded;
	MenuRGBA8 text_color;

	SfxSampleId sfx_move;
	SfxSampleId sfx_select;
	SfxSampleId sfx_back;

	bool music_started;
	bool fatal;
} MenuScreen;

static void cursor_apply_magenta_colorkey(Image* img) {
	if (!img || !img->pixels || img->width <= 0 || img->height <= 0) {
		return;
	}
	size_t count = (size_t)img->width * (size_t)img->height;
	for (size_t i = 0; i < count; i++) {
		uint32_t px = img->pixels[i];
		// Global sprite colorkey: FF00FF (magenta) is transparent.
		if ((px & 0x00FFFFFFu) == 0x00FF00FFu) {
			img->pixels[i] = px & 0x00FFFFFFu; // alpha -> 0
		}
	}
}

static void menu_screen_destroy(Screen* s) {
	MenuScreen* self = (MenuScreen*)s;
	if (!self) {
		return;
	}
	if (self->bg_loaded) {
		image_destroy(&self->bg);
		self->bg_loaded = false;
	}
	if (self->cursor_loaded) {
		image_destroy(&self->cursor);
		self->cursor_loaded = false;
	}
	if (self->font_loaded) {
		font_system_shutdown(&self->font);
		self->font_loaded = false;
	}
	if (self->music_started) {
		midi_stop();
		self->music_started = false;
	}
	menu_asset_destroy(&self->asset);
	free(self);
}

static void menu_runtime_reset(MenuScreen* self) {
	if (!self) {
		return;
	}
	self->stack_depth = 0;
	memset(self->view_stack, 0, sizeof(self->view_stack));
	memset(self->sel_stack, 0, sizeof(self->sel_stack));
	// Root
	self->stack_depth = 1;
	self->view_stack[0] = self->asset.root_view_index >= 0 ? self->asset.root_view_index : 0;
	self->sel_stack[0] = 0;
}

static bool safe_queue_command(ConsoleCommandContext* ctx, const char* line) {
	if (!ctx || !line || !line[0]) {
		return false;
	}
	if (ctx->deferred_line_pending) {
		return false;
	}
	strncpy(ctx->deferred_line, line, sizeof(ctx->deferred_line) - 1);
	ctx->deferred_line[sizeof(ctx->deferred_line) - 1] = '\0';
	ctx->deferred_line_pending = true;
	log_info_s("menu", "Queued console command: %s", ctx->deferred_line);
	return true;
}

static bool append_quoted_arg(char* dst, size_t cap, size_t* io_len, const char* s) {
	// Use double quotes and escape backslash and double-quote. Console tokenizer supports this.
	if (!dst || cap == 0 || !io_len || !s) {
		return false;
	}
	size_t len = *io_len;
	if (len + 2 >= cap) {
		return false;
	}
	dst[len++] = '"';
	for (const char* p = s; *p; p++) {
		char ch = *p;
		if (ch == '"' || ch == '\\') {
			if (len + 2 >= cap) {
				return false;
			}
			dst[len++] = '\\';
			dst[len++] = ch;
			continue;
		}
		if ((unsigned char)ch < 0x20u) {
			// Drop control chars.
			continue;
		}
		if (len + 1 >= cap) {
			return false;
		}
		dst[len++] = ch;
	}
	if (len + 2 >= cap) {
		return false;
	}
	dst[len++] = '"';
	dst[len] = '\0';
	*io_len = len;
	return true;
}

static bool build_command_line(char* out, size_t cap, const MenuAction* a) {
	if (!out || cap == 0 || !a || a->kind != MENU_ACTION_COMMAND || !a->command || !a->command[0]) {
		return false;
	}
	out[0] = '\0';
	size_t len = 0;
	// Command name (no quoting; matches user-typed convention).
	{
		size_t n = strlen(a->command);
		if (n + 1 >= cap) {
			return false;
		}
		memcpy(out, a->command, n);
		len = n;
		out[len] = '\0';
	}
	for (int i = 0; i < a->arg_count; i++) {
		const char* arg = a->args ? a->args[i] : NULL;
		if (!arg) {
			continue;
		}
		if (len + 1 >= cap) {
			return false;
		}
		out[len++] = ' ';
		out[len] = '\0';
		if (!append_quoted_arg(out, cap, &len, arg)) {
			return false;
		}
	}
	return true;
}

static int current_view_index(const MenuScreen* self) {
	if (!self || self->stack_depth <= 0) {
		return -1;
	}
	int idx = self->view_stack[self->stack_depth - 1];
	if (idx < 0 || idx >= self->asset.view_count) {
		return -1;
	}
	return idx;
}

static int current_item_count_with_back(const MenuScreen* self) {
	int vi = current_view_index(self);
	if (vi < 0) {
		return 0;
	}
	const MenuView* v = &self->asset.views[vi];
	int n = v->item_count;
	if (self->stack_depth > 1) {
		n += 1; // implicit Back
	}
	return n;
}

static bool selection_is_back(const MenuScreen* self) {
	if (!self || self->stack_depth <= 1) {
		return false;
	}
	int vi = current_view_index(self);
	if (vi < 0) {
		return false;
	}
	const MenuView* v = &self->asset.views[vi];
	int sel = self->sel_stack[self->stack_depth - 1];
	return sel == v->item_count;
}

static void play_sfx_if_enabled(const ScreenContext* ctx, SfxSampleId sfx) {
	if (!ctx || !ctx->audio_enabled) {
		return;
	}
	(void)sfx_play(sfx, 1.0f, false);
}

static void menu_screen_on_enter(Screen* s, const ScreenContext* ctx) {
	MenuScreen* self = (MenuScreen*)s;
	if (!self || !ctx || !ctx->paths) {
		return;
	}
	self->fatal = false;
	self->music_started = false;
	self->text_color = self->asset.theme.text_color;
	// If the pause menu was opened by ESC, the input system will still report ESC
	// as pressed for this frame. Ignore ESC until it is released to avoid
	// immediately closing the menu.
	self->ignore_esc_until_released = (!self->invoked_from_timeline) && ctx->in && input_key_down(ctx->in, SDL_SCANCODE_ESCAPE);
	menu_runtime_reset(self);

	// Background
	if (self->asset.theme.background_png && self->asset.theme.background_png[0] != '\0') {
		char* bg_path = asset_path_join(ctx->paths, "Images/Menus/Backgrounds", self->asset.theme.background_png);
		if (bg_path) {
			self->bg_loaded = image_load_png(&self->bg, bg_path);
			if (!self->bg_loaded) {
				log_warn("MenuScreen: failed to load background PNG: %s", bg_path);
			}
			free(bg_path);
		}
	}

	// Cursor
	if (self->asset.theme.cursor_png && self->asset.theme.cursor_png[0] != '\0') {
		char* cur_path = asset_path_join(ctx->paths, "Images/Menus/Cursors", self->asset.theme.cursor_png);
		if (cur_path) {
			self->cursor_loaded = image_load_png(&self->cursor, cur_path);
			if (!self->cursor_loaded) {
				log_warn("MenuScreen: failed to load cursor PNG: %s", cur_path);
			} else {
				cursor_apply_magenta_colorkey(&self->cursor);
			}
			free(cur_path);
		}
	}

	// Font (required; fallback to ProggyClean.ttf)
	{
		int atlas = 512;
		int size_px = self->asset.theme.text_size_px > 0 ? self->asset.theme.text_size_px : 18;
		const char* font = self->asset.theme.font_ttf;
		self->font_loaded = font_system_init(&self->font, font, size_px, atlas, atlas, ctx->paths);
		if (!self->font_loaded) {
			log_warn("MenuScreen: failed to init font '%s', falling back to ProggyClean.ttf", font ? font : "(null)");
			self->font_loaded = font_system_init(&self->font, "ProggyClean.ttf", 16, atlas, atlas, ctx->paths);
			if (!self->font_loaded) {
				log_error("MenuScreen: failed to init any font; refusing to open menu");
				self->fatal = true;
			}
		}
	}

	// Menu SFX
	self->sfx_move = sfx_load_menu_wav(self->asset.theme.sfx.on_move_wav);
	self->sfx_select = sfx_load_menu_wav(self->asset.theme.sfx.on_select_wav);
	self->sfx_back = sfx_load_menu_wav(self->asset.theme.sfx.on_back_wav);

	// Music
	if (!self->fatal && ctx->audio_enabled && ctx->music_enabled && self->asset.theme.music_midi && self->asset.theme.music_midi[0] != '\0') {
		char* midi_path = asset_path_join(ctx->paths, "Sounds/MIDI", self->asset.theme.music_midi);
		char* sf_path = asset_path_join(ctx->paths, "Sounds/SoundFonts", "hl4mgm.sf2");
		if (midi_path && sf_path) {
			midi_stop();
			if (midi_init(sf_path) == 0) {
				midi_play(midi_path);
				self->music_started = true;
			}
		}
		free(midi_path);
		free(sf_path);
	}

	log_info_s("menu", "MenuScreen enter: '%s' mode=%s", self->asset.name ? self->asset.name : "(null)", self->invoked_from_timeline ? "timeline" : "pause");
}

static void menu_screen_on_exit(Screen* s, const ScreenContext* ctx) {
	MenuScreen* self = (MenuScreen*)s;
	(void)ctx;
	if (!self) {
		return;
	}
	if (self->music_started) {
		midi_stop();
		self->music_started = false;
	}
}

static ScreenResult menu_screen_update(Screen* s, const ScreenContext* ctx, double dt_s) {
	(void)dt_s;
	MenuScreen* self = (MenuScreen*)s;
	if (!self) {
		return SCREEN_RESULT_DONE;
	}
	if (self->fatal) {
		return SCREEN_RESULT_DONE;
	}
	if (!ctx || !ctx->allow_input || !ctx->in) {
		return SCREEN_RESULT_RUNNING;
	}

	if (!self->invoked_from_timeline && self->ignore_esc_until_released) {
		if (input_key_down(ctx->in, SDL_SCANCODE_ESCAPE)) {
			return SCREEN_RESULT_RUNNING;
		}
		self->ignore_esc_until_released = false;
	}

	int vi = current_view_index(self);
	if (vi < 0) {
		return SCREEN_RESULT_DONE;
	}
	MenuView* v = &self->asset.views[vi];

	int n_items = current_item_count_with_back(self);
	if (n_items <= 0) {
		return SCREEN_RESULT_RUNNING;
	}

	int* selp = &self->sel_stack[self->stack_depth - 1];
	int sel0 = *selp;
	bool moved = false;

	if (input_key_pressed(ctx->in, SDL_SCANCODE_UP)) {
		*selp = (*selp - 1);
		if (*selp < 0) {
			*selp = n_items - 1;
		}
		moved = true;
	}
	if (input_key_pressed(ctx->in, SDL_SCANCODE_DOWN)) {
		*selp = (*selp + 1);
		if (*selp >= n_items) {
			*selp = 0;
		}
		moved = true;
	}

	if (moved && *selp != sel0) {
		play_sfx_if_enabled(ctx, self->sfx_move);
		log_info_s("menu", "Selection changed: view=%s sel=%d", v->id ? v->id : "(null)", *selp);
	}

	// Escape rules
	if (input_key_pressed(ctx->in, SDL_SCANCODE_ESCAPE)) {
		if (self->stack_depth > 1) {
			// Treat as back from submenu.
			play_sfx_if_enabled(ctx, self->sfx_back);
			self->stack_depth--;
			log_info_s("menu", "Back (ESC): depth=%d", self->stack_depth);
		} else {
			// Root
			if (!self->invoked_from_timeline) {
				log_info_s("menu", "Close menu (pause) via ESC");
				return SCREEN_RESULT_DONE;
			}
		}
		return SCREEN_RESULT_RUNNING;
	}

	if (input_key_pressed(ctx->in, SDL_SCANCODE_RETURN)) {
		play_sfx_if_enabled(ctx, self->sfx_select);
		if (selection_is_back(self)) {
			play_sfx_if_enabled(ctx, self->sfx_back);
			self->stack_depth--;
			log_info_s("menu", "Back (item): depth=%d", self->stack_depth);
			return SCREEN_RESULT_RUNNING;
		}
		int sel = *selp;
		if (sel < 0 || sel >= v->item_count) {
			return SCREEN_RESULT_RUNNING;
		}
		MenuItem* it = &v->items[sel];
		MenuAction* a = &it->action;
		if (a->kind == MENU_ACTION_NONE || a->kind == MENU_ACTION_CLOSE) {
			return SCREEN_RESULT_DONE;
		}
		if (a->kind == MENU_ACTION_SUBMENU) {
			int next = menu_asset_find_view(&self->asset, a->submenu_id);
			if (next >= 0 && self->stack_depth < MENU_STACK_MAX) {
				self->view_stack[self->stack_depth] = next;
				self->sel_stack[self->stack_depth] = 0;
				self->stack_depth++;
				log_info_s("menu", "Enter submenu: '%s' depth=%d", a->submenu_id ? a->submenu_id : "", self->stack_depth);
			}
			return SCREEN_RESULT_RUNNING;
		}
		if (a->kind == MENU_ACTION_COMMAND) {
			char cmd[MENU_CMD_MAX];
			if (build_command_line(cmd, sizeof(cmd), a)) {
				(void)safe_queue_command(self->cmd_ctx, cmd);
			}
			return SCREEN_RESULT_RUNNING;
		}
	}

	return SCREEN_RESULT_RUNNING;
}

static void menu_draw_background(MenuScreen* self, const ScreenContext* ctx) {
	if (!self || !ctx || !ctx->fb) {
		return;
	}
	if (self->bg_loaded && self->bg.pixels && self->bg.width > 0 && self->bg.height > 0) {
		draw_blit_abgr8888_scaled_nearest(ctx->fb, 0, 0, ctx->fb->width, ctx->fb->height, self->bg.pixels, self->bg.width, self->bg.height);
	} else {
		draw_clear(ctx->fb, 0xFF000000u);
	}
}

static void menu_screen_draw(Screen* s, const ScreenContext* ctx) {
	MenuScreen* self = (MenuScreen*)s;
	if (!self || !ctx || !ctx->fb) {
		return;
	}
	menu_draw_background(self, ctx);
	if (!self->font_loaded) {
		return;
	}
	int vi = current_view_index(self);
	if (vi < 0) {
		return;
	}
	MenuView* v = &self->asset.views[vi];

	int line_h = font_line_height(&self->font, 1.0f);
	if (line_h <= 0) {
		line_h = 18;
	}
	int x_center = ctx->fb->width / 2;

	bool has_title = (v->title && v->title[0] != '\0');
	int item_count = v->item_count + (self->stack_depth > 1 ? 1 : 0);
	int total_lines = item_count + (has_title ? 1 : 0);
	int total_h = total_lines * line_h;
	int y0 = (ctx->fb->height / 2) - (total_h / 2);
	if (y0 < 0) y0 = 0;

	ColorRGBA text_c = color_rgba(self->text_color.r, self->text_color.g, self->text_color.b, self->text_color.a);

	int y = y0;
	if (has_title) {
		int w = font_measure_text_width(&self->font, v->title, 1.0f);
		int x = x_center - w / 2;
		font_draw_text(&self->font, ctx->fb, x, y, v->title, text_c, 1.0f);
		y += line_h;
	}

	// Items
	int sel = self->sel_stack[self->stack_depth - 1];
	for (int i = 0; i < item_count; i++) {
		const char* label = NULL;
		if (self->stack_depth > 1 && i == v->item_count) {
			label = "Back";
		} else {
			label = (i >= 0 && i < v->item_count && v->items[i].label) ? v->items[i].label : "";
		}
		int w = font_measure_text_width(&self->font, label, 1.0f);
		int x = x_center - w / 2;
		font_draw_text(&self->font, ctx->fb, x, y, label, text_c, 1.0f);

		if (i == sel) {
			int pad = 8;
			int render = self->asset.theme.cursor_render_size_px;
			if (render <= 0) {
				render = 32;
			}
			int cx = x - render - pad;
			int cy = y + (line_h - render) / 2;
			if (self->cursor_loaded && self->cursor.pixels) {
				draw_blit_abgr8888_scaled_nearest_alpha(ctx->fb, cx, cy, render, render, self->cursor.pixels, self->cursor.width, self->cursor.height);
			} else {
				// Fallback cursor glyph
				font_draw_text(&self->font, ctx->fb, cx, y, ">", text_c, 1.0f);
			}
		}

		y += line_h;
	}
}

static const ScreenVtable g_menu_screen_vtable = {
	.destroy = menu_screen_destroy,
	.on_enter = menu_screen_on_enter,
	.update = menu_screen_update,
	.draw = menu_screen_draw,
	.on_exit = menu_screen_on_exit,
};

Screen* menu_screen_create(MenuAsset asset, bool invoked_from_timeline, ConsoleCommandContext* cmd_ctx) {
	MenuScreen* self = (MenuScreen*)calloc(1, sizeof(MenuScreen));
	if (!self) {
		menu_asset_destroy(&asset);
		return NULL;
	}
	self->base.v = &g_menu_screen_vtable;
	self->asset = asset; // move
	self->invoked_from_timeline = invoked_from_timeline;
	self->cmd_ctx = cmd_ctx;
	self->bg_loaded = false;
	self->cursor_loaded = false;
	self->font_loaded = false;
	self->music_started = false;
	self->fatal = false;
	self->sfx_move = (SfxSampleId){0, 0};
	self->sfx_select = (SfxSampleId){0, 0};
	self->sfx_back = (SfxSampleId){0, 0};
	self->stack_depth = 0;
	return &self->base;
}
