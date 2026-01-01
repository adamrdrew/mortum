#include "game/postfx.h"

#include "render/draw.h"

#include <math.h>
#include <string.h>

static float clamp01(float x) {
	if (x < 0.0f) return 0.0f;
	if (x > 1.0f) return 1.0f;
	return x;
}

static uint8_t abgr_a(uint32_t abgr) {
	return (uint8_t)((abgr >> 24) & 0xFFu);
}

static uint32_t abgr_with_a(uint32_t abgr, uint8_t a) {
	return (abgr & 0x00FFFFFFu) | ((uint32_t)a << 24);
}

void postfx_init(PostFxSystem* self) {
	if (!self) {
		return;
	}
	memset(self, 0, sizeof(*self));
}

void postfx_reset(PostFxSystem* self) {
	if (!self) {
		return;
	}
	self->active = false;
	self->abgr_max = 0u;
	self->fade_in_s = 0.0f;
	self->hold_s = 0.0f;
	self->fade_out_s = 0.0f;
	self->t_s = 0.0f;
}

bool postfx_is_active(const PostFxSystem* self) {
	return self && self->active;
}

void postfx_trigger_color_wash(PostFxSystem* self, uint32_t abgr_max, float fade_in_s, float hold_s, float fade_out_s) {
	if (!self) {
		return;
	}

	// Always fade in/out. Keep a tiny epsilon to avoid div-by-zero.
	const float min_fade = 0.0001f;
	if (fade_in_s < min_fade) {
		fade_in_s = min_fade;
	}
	if (fade_out_s < min_fade) {
		fade_out_s = min_fade;
	}
	if (hold_s < 0.0f) {
		hold_s = 0.0f;
	}

	self->active = true;
	self->abgr_max = abgr_max;
	self->fade_in_s = fade_in_s;
	self->hold_s = hold_s;
	self->fade_out_s = fade_out_s;
	self->t_s = 0.0f;
}

void postfx_trigger_damage_flash(PostFxSystem* self) {
	// Fast but readable flash; total duration ~0.13s (~8 frames @60fps).
	postfx_trigger_color_wash(self, 0xD00000FFu, 0.02f, 0.02f, 0.09f);
}

void postfx_update(PostFxSystem* self, double dt_s) {
	if (!self || !self->active) {
		return;
	}
	if (dt_s < 0.0) {
		return;
	}
	if (dt_s > 0.25) {
		dt_s = 0.25;
	}

	self->t_s += (float)dt_s;
	float total = self->fade_in_s + self->hold_s + self->fade_out_s;
	if (self->t_s >= total) {
		postfx_reset(self);
	}
}

void postfx_draw(const PostFxSystem* self, Framebuffer* fb) {
	if (!self || !self->active || !fb) {
		return;
	}

	float total = self->fade_in_s + self->hold_s + self->fade_out_s;
	float t = self->t_s;
	if (total <= 0.0f) {
		return;
	}

	float k = 0.0f;
	if (t < self->fade_in_s) {
		k = self->fade_in_s > 0.0f ? (t / self->fade_in_s) : 1.0f;
	} else if (t < self->fade_in_s + self->hold_s) {
		k = 1.0f;
	} else {
		float out_t = t - self->fade_in_s - self->hold_s;
		k = self->fade_out_s > 0.0f ? (1.0f - out_t / self->fade_out_s) : 0.0f;
	}
	k = clamp01(k);

	uint8_t a0 = abgr_a(self->abgr_max);
	uint8_t a = (uint8_t)lroundf((float)a0 * k);
	if (a == 0u) {
		return;
	}

	draw_rect_abgr8888_alpha(fb, 0, 0, fb->width, fb->height, abgr_with_a(self->abgr_max, a));
}
