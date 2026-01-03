#include "game/notifications.h"

#include "render/draw.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static inline float clampf(float v, float lo, float hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

static inline uint32_t abgr_from_rgba8(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	// ABGR8888 = AABBGGRR
	return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline ColorRGBA color_from_abgr(uint32_t abgr) {
	return (ColorRGBA){
		.r = (uint8_t)(abgr & 0xFFu),
		.g = (uint8_t)((abgr >> 8) & 0xFFu),
		.b = (uint8_t)((abgr >> 16) & 0xFFu),
		.a = (uint8_t)((abgr >> 24) & 0xFFu),
	};
}

static void notification_item_clear(NotificationItem* it) {
	if (!it) {
		return;
	}
	it->text[0] = '\0';
	it->has_icon = false;
	it->icon_filename[0] = '\0';
}

void notifications_init(Notifications* self) {
	if (!self) {
		return;
	}
	memset(self, 0, sizeof(*self));
	notification_item_clear(&self->cur);
}

void notifications_reset(Notifications* self) {
	if (!self) {
		return;
	}
	self->head = 0u;
	self->count = 0u;
	self->active = false;
	self->phase = NOTIFY_PHASE_IN;
	self->phase_t_s = 0.0f;
	self->hold_t_s = 0.0f;
	self->hold_target_s = 0.0f;
	for (uint32_t i = 0u; i < NOTIFICATIONS_QUEUE_CAP; i++) {
		notification_item_clear(&self->queue[i]);
	}
	notification_item_clear(&self->cur);
}

static bool safe_copy_str(char* dst, size_t dst_cap, const char* src) {
	if (!dst || dst_cap == 0) {
		return false;
	}
	dst[0] = '\0';
	if (!src || src[0] == '\0') {
		return false;
	}
	size_t n = strlen(src);
	if (n >= dst_cap) {
		// Truncate.
		n = dst_cap - 1;
	}
	memcpy(dst, src, n);
	dst[n] = '\0';
	return true;
}

static bool notifications_enqueue(Notifications* self, const char* text, const char* icon_filename) {
	if (!self || !text || text[0] == '\0') {
		return false;
	}
	if (self->count >= NOTIFICATIONS_QUEUE_CAP) {
		return false;
	}
	uint32_t idx = (self->head + self->count) % NOTIFICATIONS_QUEUE_CAP;
	NotificationItem* it = &self->queue[idx];
	notification_item_clear(it);
	if (!safe_copy_str(it->text, sizeof(it->text), text)) {
		notification_item_clear(it);
		return false;
	}
	if (icon_filename && icon_filename[0] != '\0') {
		it->has_icon = safe_copy_str(it->icon_filename, sizeof(it->icon_filename), icon_filename);
		if (!it->has_icon) {
			it->icon_filename[0] = '\0';
		}
	}
	self->count++;
	return true;
}

bool notifications_push_text(Notifications* self, const char* text) {
	return notifications_enqueue(self, text, NULL);
}

bool notifications_push_icon(Notifications* self, const char* text, const char* icon_filename) {
	return notifications_enqueue(self, text, icon_filename);
}

static bool notifications_dequeue(Notifications* self, NotificationItem* out) {
	if (!self || !out) {
		return false;
	}
	if (self->count == 0u) {
		return false;
	}
	*out = self->queue[self->head];
	notification_item_clear(&self->queue[self->head]);
	self->head = (self->head + 1u) % NOTIFICATIONS_QUEUE_CAP;
	self->count--;
	return true;
}

static void notifications_start_next(Notifications* self) {
	if (!self) {
		return;
	}
	NotificationItem next;
	notification_item_clear(&next);
	if (!notifications_dequeue(self, &next)) {
		self->active = false;
		return;
	}
	self->cur = next;
	self->active = true;
	self->phase = NOTIFY_PHASE_IN;
	self->phase_t_s = 0.0f;
	self->hold_t_s = 0.0f;
	self->hold_target_s = 0.0f;
}

void notifications_tick(Notifications* self, float dt_s) {
	if (!self) {
		return;
	}
	if (dt_s < 0.0f) {
		dt_s = 0.0f;
	}
	if (dt_s > 0.25f) {
		dt_s = 0.25f;
	}

	if (!self->active) {
		if (self->count > 0u) {
			notifications_start_next(self);
		}
		return;
	}

	// Sane defaults (tweak later).
	const float in_s = 0.25f;
	const float base_hold_s = 2.0f;
	const float out_s = 0.25f;

	self->phase_t_s += dt_s;
	if (self->phase == NOTIFY_PHASE_IN) {
		if (self->phase_t_s >= in_s) {
			self->phase = NOTIFY_PHASE_HOLD;
			self->phase_t_s = 0.0f;
			self->hold_t_s = 0.0f;
		}
		return;
	}
	if (self->phase == NOTIFY_PHASE_HOLD) {
		self->hold_t_s += dt_s;
		float hold_s = self->hold_target_s;
		if (hold_s <= 0.0f) {
			hold_s = base_hold_s;
		}
		if (self->phase_t_s >= hold_s) {
			self->phase = NOTIFY_PHASE_OUT;
			self->phase_t_s = 0.0f;
		}
		return;
	}
	// OUT
	if (self->phase_t_s >= out_s) {
		// Advance to next.
		self->active = false;
		notification_item_clear(&self->cur);
		self->phase = NOTIFY_PHASE_IN;
		self->phase_t_s = 0.0f;
		self->hold_t_s = 0.0f;
		if (self->count > 0u) {
			notifications_start_next(self);
		}
	}
}

static uint32_t alpha_zero_if_magenta(uint32_t abgr) {
	// Treat FF00FF (magenta) as transparent regardless of alpha.
	if ((abgr & 0x00FFFFFFu) == 0x00FF00FFu) {
		return 0u;
	}
	return abgr;
}

static void build_icon_24x24_abgr_alpha(uint32_t out_px[24 * 24], const Texture* src) {
	if (!out_px) {
		return;
	}
	for (int i = 0; i < 24 * 24; i++) {
		out_px[i] = 0u;
	}
	if (!src || !src->pixels || src->width <= 0 || src->height <= 0) {
		return;
	}

	// Nearest sampling.
	for (int y = 0; y < 24; y++) {
		int sy = (int)((int64_t)y * (int64_t)src->height / 24);
		if (sy < 0) sy = 0;
		if (sy >= src->height) sy = src->height - 1;
		for (int x = 0; x < 24; x++) {
			int sx = (int)((int64_t)x * (int64_t)src->width / 24);
			if (sx < 0) sx = 0;
			if (sx >= src->width) sx = src->width - 1;
			uint32_t p = src->pixels[sy * src->width + sx];
			p = alpha_zero_if_magenta(p);
			// If the source has no alpha channel (e.g., BMP), assume fully opaque.
			if (((p >> 24) & 0xFFu) == 0u && (p & 0x00FFFFFFu) != 0u) {
				p |= 0xFFu << 24;
			}
			out_px[y * 24 + x] = p;
		}
	}
}

static int prefix_width_px(FontSystem* font, const char* s, int n, float scale) {
	if (!font || !s || n <= 0) {
		return 0;
	}
	char tmp[NOTIFICATION_TEXT_MAX];
	size_t cap = sizeof(tmp);
	size_t nn = (size_t)n;
	if (nn >= cap) {
		nn = cap - 1;
	}
	memcpy(tmp, s, nn);
	tmp[nn] = '\0';
	return font_measure_text_width(font, tmp, scale);
}

static int find_start_index_for_scroll(FontSystem* font, const char* text, float scale, int scroll_px) {
	if (!font || !text || scroll_px <= 0) {
		return 0;
	}
	int n = (int)strlen(text);
	if (n <= 0) {
		return 0;
	}
	// Linear scan with prefix measurements (small strings; deterministic and allocation-free).
	for (int i = 0; i < n; i++) {
		int w = prefix_width_px(font, text, i, scale);
		if (w >= scroll_px) {
			return i;
		}
	}
	return n;
}

static void build_window_text(FontSystem* font, const char* text, float scale, int start_idx, int max_w, bool add_ellipsis, char* out, size_t out_cap) {
	if (!out || out_cap == 0) {
		return;
	}
	out[0] = '\0';
	if (!font || !text || max_w <= 0) {
		return;
	}
	int n = (int)strlen(text);
	if (n <= 0) {
		return;
	}
	if (start_idx < 0) {
		start_idx = 0;
	}
	if (start_idx > n) {
		start_idx = n;
	}

	const char* s = text + start_idx;
	int avail = max_w;
	int ell_w = 0;
	if (add_ellipsis) {
		ell_w = font_measure_text_width(font, "...", scale);
		if (ell_w >= avail) {
			return;
		}
		avail -= ell_w;
	}

	// Find the largest prefix of s that fits.
	int best = 0;
	int rem_n = (int)strlen(s);
	for (int i = 1; i <= rem_n; i++) {
		int w = prefix_width_px(font, s, i, scale);
		if (w <= avail) {
			best = i;
		} else {
			break;
		}
	}

	size_t keep = (size_t)best;
	if (keep >= out_cap) {
		keep = out_cap - 1;
	}
	memcpy(out, s, keep);
	out[keep] = '\0';
	if (add_ellipsis) {
		strncat(out, "...", out_cap - strlen(out) - 1);
	}
}

void notifications_draw(
	Notifications* self,
	Framebuffer* fb,
	FontSystem* font,
	TextureRegistry* texreg,
	const AssetPaths* paths
) {
	if (!self || !self->active || !fb || !fb->pixels || !font) {
		return;
	}

	// Visual defaults.
	const uint32_t toast_bg = abgr_from_rgba8(64, 64, 64, 255);
	const uint32_t toast_border = abgr_from_rgba8(96, 96, 96, 255);
	const uint32_t icon_bg = abgr_from_rgba8(0, 0, 0, 255);
	const ColorRGBA text_col = color_from_abgr(abgr_from_rgba8(255, 0, 0, 255));

	const int margin = 12;
	const int pad = 8;
	const int icon_sz = 24;
	const int gap = 8;
	const float text_scale = 1.0f;
	const float base_hold_s = 2.0f;

	int line_h = font_line_height(font, text_scale);
	if (line_h < icon_sz) {
		line_h = icon_sz;
	}
	int toast_h = line_h + pad * 2;
	int max_w = 240;
	if (fb->width - margin * 2 < max_w) {
		max_w = fb->width - margin * 2;
	}
	if (max_w < 160) {
		max_w = 160;
	}
	int toast_w = max_w;

	// Slide animation.
	const float in_s = 0.25f;
	const float out_s = 0.25f;
	float slide = 0.0f;
	if (self->phase == NOTIFY_PHASE_IN) {
		slide = 1.0f - clampf(self->phase_t_s / in_s, 0.0f, 1.0f);
	} else if (self->phase == NOTIFY_PHASE_OUT) {
		slide = clampf(self->phase_t_s / out_s, 0.0f, 1.0f);
	} else {
		slide = 0.0f;
	}
	int off_x = (int)roundf(slide * (float)(toast_w + margin));
	int x = fb->width - margin - toast_w + off_x;
	int y = margin;

	// Background + thin border.
	draw_rect(fb, x, y, toast_w, toast_h, toast_bg);
	draw_rect(fb, x, y, toast_w, 1, toast_border);
	draw_rect(fb, x, y + toast_h - 1, toast_w, 1, toast_border);
	draw_rect(fb, x, y, 1, toast_h, toast_border);
	draw_rect(fb, x + toast_w - 1, y, 1, toast_h, toast_border);

	int content_x = x + pad;
	int content_y = y + pad;
	int avail_w = toast_w - pad * 2;

	// Optional icon.
	if (self->cur.has_icon && texreg && paths && self->cur.icon_filename[0] != '\0') {
		const Texture* t = texture_registry_get(texreg, paths, self->cur.icon_filename);
		// Icon bg always draws, even if texture missing, for consistent layout.
		draw_rect(fb, content_x, content_y + (line_h - icon_sz) / 2, icon_sz, icon_sz, icon_bg);
		if (t && t->pixels) {
			uint32_t icon_px[24 * 24];
			build_icon_24x24_abgr_alpha(icon_px, t);
			draw_blit_abgr8888_alpha(fb, content_x, content_y + (line_h - icon_sz) / 2, icon_px, 24, 24);
		}
		content_x += icon_sz + gap;
		avail_w -= icon_sz + gap;
	}

	if (avail_w <= 0) {
		return;
	}

	const char* text = self->cur.text;
	if (!text || text[0] == '\0') {
		return;
	}

	// Overflow handling: show a truncated view first, then horizontally scroll.
	int full_w = font_measure_text_width(font, text, text_scale);
	bool overflow = (full_w > avail_w);

	char render[NOTIFICATION_TEXT_MAX];
	render[0] = '\0';
	if (!overflow) {
		safe_copy_str(render, sizeof(render), text);
	} else {
		// Initial delay before scrolling so the user can read the start.
		const float scroll_delay_s = 0.35f;
		const float scroll_speed_px_s = 95.0f;
		const float end_pause_s = 0.50f;
		int overflow_px = full_w - avail_w;
		if (overflow_px < 0) {
			overflow_px = 0;
		}

		// Ensure we don't dismiss before the scroll has reached the end.
		// We set the hold target dynamically based on measured overflow.
		float needed = scroll_delay_s + ((float)overflow_px / scroll_speed_px_s) + end_pause_s;
		if (needed < base_hold_s) {
			needed = base_hold_s;
		}
		// Only ever extend; never shrink mid-toast.
		if (self->hold_target_s <= 0.0f || self->hold_target_s < needed) {
			self->hold_target_s = needed;
		}

		float t = 0.0f;
		if (self->phase == NOTIFY_PHASE_HOLD) {
			t = self->hold_t_s;
		}
		if (t <= scroll_delay_s) {
			build_window_text(font, text, text_scale, 0, avail_w, true, render, sizeof(render));
		} else {
			float scroll_t = t - scroll_delay_s;
			float scroll_px_f = scroll_t * scroll_speed_px_s;
			// Clamp at end (no looping). We rely on hold_target to keep the toast visible.
			if (scroll_px_f > (float)overflow_px) {
				scroll_px_f = (float)overflow_px;
			}
			int start = find_start_index_for_scroll(font, text, text_scale, (int)scroll_px_f);
			build_window_text(font, text, text_scale, start, avail_w, false, render, sizeof(render));
		}
	}

	font_draw_text(font, fb, content_x, content_y + (line_h - font_line_height(font, text_scale)) / 2, render, text_col, text_scale);
}
