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

static float effect_total_s(const PostFxEffect* e) {
	return e->fade_in_s + e->hold_s + e->fade_out_s;
}

static uint8_t effect_alpha_now(const PostFxEffect* e) {
	if (!e || !e->active) {
		return 0u;
	}
	float total = effect_total_s(e);
	if (total <= 0.0f) {
		return 0u;
	}
	float t = e->t_s;
	float k = 0.0f;
	if (t < e->fade_in_s) {
		k = e->fade_in_s > 0.0f ? (t / e->fade_in_s) : 1.0f;
	} else if (t < e->fade_in_s + e->hold_s) {
		k = 1.0f;
	} else {
		float out_t = t - e->fade_in_s - e->hold_s;
		k = e->fade_out_s > 0.0f ? (1.0f - out_t / e->fade_out_s) : 0.0f;
	}
	k = clamp01(k);
	uint8_t a0 = abgr_a(e->abgr_max);
	return (uint8_t)lroundf((float)a0 * k);
}

static int find_slot_by_tag(const PostFxSystem* self, PostFxTag tag) {
	if (!self || tag == POSTFX_TAG_NONE) {
		return -1;
	}
	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		if (self->effects[i].active && self->effects[i].tag == tag) {
			return i;
		}
	}
	return -1;
}

static int find_free_slot(const PostFxSystem* self) {
	if (!self) {
		return -1;
	}
	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		if (!self->effects[i].active) {
			return i;
		}
	}
	return -1;
}

static int find_evict_candidate(const PostFxSystem* self, int new_priority) {
	// If full, evict the lowest priority effect (oldest among ties), but only
	// if it is <= new_priority. Otherwise, drop the new effect.
	if (!self) {
		return -1;
	}
	int best_i = -1;
	int best_pri = 0;
	uint32_t best_serial = 0;
	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		const PostFxEffect* e = &self->effects[i];
		if (!e->active) {
			continue;
		}
		if (best_i < 0 || e->priority < best_pri || (e->priority == best_pri && e->serial < best_serial)) {
			best_i = i;
			best_pri = e->priority;
			best_serial = e->serial;
		}
	}
	if (best_i < 0) {
		return -1;
	}
	if (best_pri > new_priority) {
		return -1;
	}
	return best_i;
}

void postfx_init(PostFxSystem* self) {
	if (!self) {
		return;
	}
	memset(self, 0, sizeof(*self));
	self->next_serial = 1u;
}

void postfx_reset(PostFxSystem* self) {
	if (!self) {
		return;
	}
	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		self->effects[i].active = false;
		self->effects[i].tag = POSTFX_TAG_NONE;
		self->effects[i].priority = 0;
		self->effects[i].serial = 0u;
		self->effects[i].abgr_max = 0u;
		self->effects[i].fade_in_s = 0.0f;
		self->effects[i].hold_s = 0.0f;
		self->effects[i].fade_out_s = 0.0f;
		self->effects[i].t_s = 0.0f;
	}
}

bool postfx_is_active(const PostFxSystem* self) {
	if (!self) {
		return false;
	}
	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		if (self->effects[i].active) {
			return true;
		}
	}
	return false;
}

void postfx_trigger_color_wash(PostFxSystem* self, uint32_t abgr_max, float fade_in_s, float hold_s, float fade_out_s) {
	postfx_trigger_tagged_color_wash(self, POSTFX_TAG_NONE, 0, abgr_max, fade_in_s, hold_s, fade_out_s);
}

void postfx_trigger_tagged_color_wash(
	PostFxSystem* self,
	PostFxTag tag,
	int priority,
	uint32_t abgr_max,
	float fade_in_s,
	float hold_s,
	float fade_out_s
) {
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

	int slot = -1;
	if (tag != POSTFX_TAG_NONE) {
		slot = find_slot_by_tag(self, tag);
	}
	if (slot < 0) {
		slot = find_free_slot(self);
	}
	if (slot < 0) {
		slot = find_evict_candidate(self, priority);
	}
	if (slot < 0) {
		// Pool is full and all effects have higher priority than this new one.
		return;
	}

	PostFxEffect* e = &self->effects[slot];
	e->active = true;
	e->tag = tag;
	e->priority = priority;
	e->serial = self->next_serial++;
	e->abgr_max = abgr_max;
	e->fade_in_s = fade_in_s;
	e->hold_s = hold_s;
	e->fade_out_s = fade_out_s;
	e->t_s = 0.0f;
}

void postfx_clear_tag(PostFxSystem* self, PostFxTag tag) {
	if (!self || tag == POSTFX_TAG_NONE) {
		return;
	}
	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		if (self->effects[i].active && self->effects[i].tag == tag) {
			self->effects[i].active = false;
		}
	}
}

void postfx_trigger_damage_flash(PostFxSystem* self) {
	// Fast but readable flash; total duration ~0.13s (~8 frames @60fps).
	postfx_trigger_tagged_color_wash(self, POSTFX_TAG_DAMAGE_FLASH, 100, 0xD00000FFu, 0.02f, 0.02f, 0.09f);
}

void postfx_update(PostFxSystem* self, double dt_s) {
	if (!self) {
		return;
	}
	if (dt_s < 0.0) {
		return;
	}
	if (dt_s > 0.25) {
		dt_s = 0.25;
	}

	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		PostFxEffect* e = &self->effects[i];
		if (!e->active) {
			continue;
		}
		e->t_s += (float)dt_s;
		float total = effect_total_s(e);
		if (e->t_s >= total) {
			e->active = false;
		}
	}
}

void postfx_draw(const PostFxSystem* self, Framebuffer* fb) {
	if (!self || !fb) {
		return;
	}

	// Draw active effects in stable order: low priority first (under),
	// higher priority later (over). For equal priority, older first.
	int idx[POSTFX_MAX_EFFECTS];
	int n = 0;
	for (int i = 0; i < POSTFX_MAX_EFFECTS; i++) {
		if (self->effects[i].active) {
			idx[n++] = i;
		}
	}
	if (n <= 0) {
		return;
	}
	for (int i = 0; i < n; i++) {
		for (int j = i + 1; j < n; j++) {
			const PostFxEffect* a = &self->effects[idx[i]];
			const PostFxEffect* b = &self->effects[idx[j]];
			bool swap = false;
			if (b->priority < a->priority) {
				swap = true;
			} else if (b->priority == a->priority && b->serial < a->serial) {
				swap = true;
			}
			if (swap) {
				int tmp = idx[i];
				idx[i] = idx[j];
				idx[j] = tmp;
			}
		}
	}

	for (int i = 0; i < n; i++) {
		const PostFxEffect* e = &self->effects[idx[i]];
		uint8_t a = effect_alpha_now(e);
		if (a == 0u) {
			continue;
		}
		draw_rect_abgr8888_alpha(fb, 0, 0, fb->width, fb->height, abgr_with_a(e->abgr_max, a));
	}
}
