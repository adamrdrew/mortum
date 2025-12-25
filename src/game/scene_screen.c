#include "game/scene_screen.h"

#include "assets/asset_paths.h"
#include "assets/image.h"
#include "assets/midi_player.h"

#include "core/log.h"

#include "game/font.h"

#include "platform/audio.h"

#include "render/draw.h"

#include <SDL.h>

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct SceneScreen {
	Screen base;
	Scene scene;

	Image bg;
	bool bg_loaded;

	FontSystem font;
	bool font_loaded;

	double t_s;
	bool exiting;
	double exit_t_s;

	SfxVoiceId enter_voice;
	SfxVoiceId exit_voice;
	bool music_started;
} SceneScreen;

static uint32_t abgr_from_rgba8(SceneRGBA8 c) {
	return ((uint32_t)c.a << 24) | ((uint32_t)c.b << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.r;
}

static float clamp01(float x) {
	if (x < 0.0f) return 0.0f;
	if (x > 1.0f) return 1.0f;
	return x;
}

static bool any_key_pressed_no_repeat(const Input* in) {
	if (!in) {
		return false;
	}
	for (int i = 0; i < in->key_event_count; i++) {
		if (!in->key_events[i].repeat) {
			return true;
		}
	}
	return false;
}

static void scene_screen_destroy(Screen* s) {
	SceneScreen* self = (SceneScreen*)s;
	if (!self) {
		return;
	}
	if (self->bg_loaded) {
		image_destroy(&self->bg);
		self->bg_loaded = false;
	}
	if (self->font_loaded) {
		font_system_shutdown(&self->font);
		self->font_loaded = false;
	}
	scene_destroy(&self->scene);
	free(self);
}

static void scene_screen_on_enter(Screen* s, const ScreenContext* ctx) {
	SceneScreen* self = (SceneScreen*)s;
	if (!self || !ctx || !ctx->paths) {
		return;
	}
	self->t_s = 0.0;
	self->exiting = false;
	self->exit_t_s = 0.0;
	self->enter_voice = (SfxVoiceId){0, 0};
	self->exit_voice = (SfxVoiceId){0, 0};
	self->music_started = false;

	// Background
	if (self->scene.background_png && self->scene.background_png[0] != '\0') {
		char* bg_path = asset_path_join(ctx->paths, "Images", self->scene.background_png);
		if (bg_path) {
			self->bg_loaded = image_load_png(&self->bg, bg_path);
			if (!self->bg_loaded) {
				log_warn("SceneScreen: failed to load background PNG: %s", bg_path);
			}
			free(bg_path);
		}
	}

	// Text font
	if (self->scene.text.enabled && self->scene.text.font_file && self->scene.text.font_file[0] != '\0') {
		int atlas = self->scene.text.atlas_size > 0 ? self->scene.text.atlas_size : 512;
		int size_px = self->scene.text.size_px > 0 ? self->scene.text.size_px : 16;
		self->font_loaded = font_system_init(&self->font, self->scene.text.font_file, size_px, atlas, atlas, ctx->paths);
		if (!self->font_loaded) {
			log_warn("SceneScreen: failed to init font: %s", self->scene.text.font_file);
		}
	}

	// Enter SFX
	if (ctx->audio_enabled && self->scene.sfx.enter_wav && self->scene.sfx.enter_wav[0] != '\0') {
		SfxSampleId sfx = sfx_load_effect_wav(self->scene.sfx.enter_wav);
		self->enter_voice = sfx_play(sfx, 1.0f, false);
	}

	// Music
	if (ctx->audio_enabled && ctx->music_enabled && self->scene.music.midi_file && self->scene.music.midi_file[0] != '\0') {
		const char* sf = self->scene.music.soundfont_file ? self->scene.music.soundfont_file : "hl4mgm.sf2";
		char* midi_path = asset_path_join(ctx->paths, "Sounds/MIDI", self->scene.music.midi_file);
		char* sf_path = asset_path_join(ctx->paths, "Sounds/SoundFonts", sf);
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
}

static void scene_screen_on_exit(Screen* s, const ScreenContext* ctx) {
	SceneScreen* self = (SceneScreen*)s;
	if (!self || !ctx) {
		return;
	}
	if (ctx->audio_enabled && self->music_started) {
		midi_stop();
	}
	if (ctx->audio_enabled && self->scene.sfx.exit_wav && self->scene.sfx.exit_wav[0] != '\0') {
		SfxSampleId sfx = sfx_load_effect_wav(self->scene.sfx.exit_wav);
		self->exit_voice = sfx_play(sfx, 1.0f, false);
	}
}

static double scene_time_to_exit_s(const SceneScreen* self) {
	if (!self) {
		return 0.0;
	}
	double fade_out_s = 0.0;
	if (self->scene.fade_out.enabled && self->scene.fade_out.duration_ms > 0) {
		fade_out_s = (double)self->scene.fade_out.duration_ms / 1000.0;
	}
	return fade_out_s;
}

static bool scene_should_end(SceneScreen* self, const ScreenContext* ctx) {
	if (!self || !ctx) {
		return false;
	}
	if (self->scene.end.timeout_ms > 0) {
		double t_limit = (double)self->scene.end.timeout_ms / 1000.0;
		if (self->t_s >= t_limit) {
			return true;
		}
	}
	if (!ctx->allow_input) {
		return false;
	}
	if (self->scene.end.any_key) {
		if (any_key_pressed_no_repeat(ctx->in)) {
			return true;
		}
	}
	if (self->scene.end.scancode >= 0) {
		if (input_key_pressed(ctx->in, self->scene.end.scancode)) {
			return true;
		}
	}
	return false;
}

static ScreenResult scene_screen_update(Screen* s, const ScreenContext* ctx, double dt_s) {
	SceneScreen* self = (SceneScreen*)s;
	if (!self) {
		return SCREEN_RESULT_DONE;
	}
	if (dt_s < 0.0) {
		dt_s = 0.0;
	}
	if (dt_s > 0.25) {
		dt_s = 0.25;
	}

	if (!self->exiting) {
		self->t_s += dt_s;
		if (scene_should_end(self, ctx)) {
			self->exiting = true;
			self->exit_t_s = 0.0;
		}
		return SCREEN_RESULT_RUNNING;
	}

	self->exit_t_s += dt_s;
	if (self->exit_t_s >= scene_time_to_exit_s(self)) {
		return SCREEN_RESULT_DONE;
	}
	return SCREEN_RESULT_RUNNING;
}

static void scene_draw_background(SceneScreen* self, const ScreenContext* ctx) {
	if (!self || !ctx || !ctx->fb) {
		return;
	}
	if (self->bg_loaded && self->bg.pixels && self->bg.width > 0 && self->bg.height > 0) {
		draw_blit_abgr8888_scaled_nearest(
			ctx->fb,
			0,
			0,
			ctx->fb->width,
			ctx->fb->height,
			self->bg.pixels,
			self->bg.width,
			self->bg.height
		);
	} else {
		// Fallback: solid black.
		draw_clear(ctx->fb, 0xFF000000u);
	}
}

static void scene_draw_text(SceneScreen* self, const ScreenContext* ctx) {
	if (!self || !ctx || !ctx->fb) {
		return;
	}
	if (!self->scene.text.enabled || !self->scene.text.text || !self->font_loaded) {
		return;
	}

	int w = font_measure_text_width(&self->font, self->scene.text.text, 1.0f);
	int x_anchor = self->scene.text.x_px;
	int y = self->scene.text.y_px;
	if (x_anchor < 0) {
		x_anchor = ctx->fb->width / 2;
	}
	if (y < 0) {
		y = ctx->fb->height / 2;
	}

	int x = x_anchor;
	if (self->scene.text.align == SCENE_TEXT_ALIGN_CENTER) {
		x = x_anchor - w / 2;
	} else if (self->scene.text.align == SCENE_TEXT_ALIGN_RIGHT) {
		x = x_anchor - w;
	}

	float scroll_px = 0.0f;
	if (self->scene.text.scroll) {
		scroll_px = (float)self->t_s * self->scene.text.scroll_speed_px_s;
	}

	float opacity = clamp01(self->scene.text.opacity);
	uint8_t a = (uint8_t)lroundf((float)self->scene.text.color.a * opacity);
	ColorRGBA c = color_rgba(self->scene.text.color.r, self->scene.text.color.g, self->scene.text.color.b, a);
	font_draw_text(&self->font, ctx->fb, x, (int)lroundf((float)y - scroll_px), self->scene.text.text, c, 1.0f);
}

static void scene_draw_fade(SceneScreen* self, const ScreenContext* ctx) {
	if (!self || !ctx || !ctx->fb) {
		return;
	}

	// Fade-in overlays at the beginning (from color -> transparent).
	if (self->scene.fade_in.enabled && self->scene.fade_in.duration_ms > 0) {
		double dur = (double)self->scene.fade_in.duration_ms / 1000.0;
		double t = self->t_s;
		if (t < dur) {
			float k = (float)(1.0 - (t / dur));
			k = clamp01(k);
			SceneRGBA8 c = self->scene.fade_in.from;
			uint8_t a = (uint8_t)lroundf((float)c.a * k);
			draw_rect_abgr8888_alpha(ctx->fb, 0, 0, ctx->fb->width, ctx->fb->height, abgr_from_rgba8((SceneRGBA8){c.r, c.g, c.b, a}));
		}
	}

	// Fade-out overlays once exiting (transparent -> from color).
	if (self->scene.fade_out.enabled && self->scene.fade_out.duration_ms > 0 && self->exiting) {
		double dur = (double)self->scene.fade_out.duration_ms / 1000.0;
		double t = self->exit_t_s;
		if (t < dur) {
			float k = (float)(t / dur);
			k = clamp01(k);
			SceneRGBA8 c = self->scene.fade_out.from;
			uint8_t a = (uint8_t)lroundf((float)c.a * k);
			draw_rect_abgr8888_alpha(ctx->fb, 0, 0, ctx->fb->width, ctx->fb->height, abgr_from_rgba8((SceneRGBA8){c.r, c.g, c.b, a}));
		}
	}
}

static void scene_screen_draw(Screen* s, const ScreenContext* ctx) {
	SceneScreen* self = (SceneScreen*)s;
	if (!self || !ctx || !ctx->fb) {
		return;
	}
	scene_draw_background(self, ctx);
	scene_draw_text(self, ctx);
	scene_draw_fade(self, ctx);
}

static const ScreenVtable g_scene_screen_vtable = {
	.destroy = scene_screen_destroy,
	.on_enter = scene_screen_on_enter,
	.update = scene_screen_update,
	.draw = scene_screen_draw,
	.on_exit = scene_screen_on_exit,
};

Screen* scene_screen_create(Scene scene) {
	SceneScreen* self = (SceneScreen*)calloc(1, sizeof(SceneScreen));
	if (!self) {
		scene_destroy(&scene);
		return NULL;
	}
	self->base.v = &g_scene_screen_vtable;
	self->scene = scene; // move
	self->bg_loaded = false;
	self->font_loaded = false;
	self->t_s = 0.0;
	self->exiting = false;
	self->exit_t_s = 0.0;
	return &self->base;
}
