#include "game/hud.h"

#include "game/ammo.h"
#include "game/weapon_defs.h"
#include "game/weapon_visuals.h"

#include "core/log.h"

#include "render/draw.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static inline ColorRGBA color_from_abgr(uint32_t abgr) {
	ColorRGBA c;
	c.a = (uint8_t)((abgr >> 24) & 0xFFu);
	c.b = (uint8_t)((abgr >> 16) & 0xFFu);
	c.g = (uint8_t)((abgr >> 8) & 0xFFu);
	c.r = (uint8_t)(abgr & 0xFFu);
	return c;
}

static inline uint8_t abgr_a(uint32_t abgr) {
	return (uint8_t)((abgr >> 24) & 0xFFu);
}

static int count_bits_u32(unsigned int v) {
	int n = 0;
	while (v) {
		n += (int)(v & 1u);
		v >>= 1u;
	}
	return n;
}

static void draw_rect_solid_or_alpha(Framebuffer* fb, int x, int y, int w, int h, uint32_t abgr) {
	if (!fb || w <= 0 || h <= 0) {
		return;
	}
	if (abgr_a(abgr) >= 255u) {
		draw_rect(fb, x, y, w, h, abgr);
	} else if (abgr_a(abgr) > 0u) {
		draw_rect_abgr8888_alpha(fb, x, y, w, h, abgr);
	}
}

static void draw_bevel(Framebuffer* fb, int x, int y, int w, int h, const HudBevel* bev) {
	if (!fb || !bev || !bev->enabled) {
		return;
	}
	int t = bev->thickness_px;
	if (t <= 0 || w <= 0 || h <= 0) {
		return;
	}
	if (t * 2 > w) {
		t = w / 2;
	}
	if (t * 2 > h) {
		t = h / 2;
	}
	if (t <= 0) {
		return;
	}

	// Top + left highlight
	draw_rect_solid_or_alpha(fb, x, y, w, t, bev->hi_abgr);
	draw_rect_solid_or_alpha(fb, x, y, t, h, bev->hi_abgr);
	// Bottom + right shadow
	draw_rect_solid_or_alpha(fb, x, y + h - t, w, t, bev->lo_abgr);
	draw_rect_solid_or_alpha(fb, x + w - t, y, t, h, bev->lo_abgr);
}

static void draw_background(Framebuffer* fb, int x, int y, int w, int h, const HudBackground* bg, const Texture* tex) {
	if (!fb || !bg || w <= 0 || h <= 0) {
		return;
	}
	if (bg->mode == HUD_BACKGROUND_IMAGE) {
		if (tex && tex->pixels && tex->width > 0 && tex->height > 0) {
			draw_blit_abgr8888_scaled_nearest_alpha(fb, x, y, w, h, tex->pixels, tex->width, tex->height);
			return;
		}
		// Fallback (should not happen if init/reload succeeded).
	}
	draw_rect_solid_or_alpha(fb, x, y, w, h, bg->color_abgr);
}

static float choose_text_scale(FontSystem* fs, const char* text, int max_w, float min_scale, float max_scale) {
	if (!fs || !text || max_w <= 0) {
		return min_scale;
	}
	if (max_scale < min_scale) {
		float tmp = max_scale;
		max_scale = min_scale;
		min_scale = tmp;
	}
	if (max_scale <= 0.0f) {
		return max_scale;
	}
	if (min_scale <= 0.0f) {
		min_scale = 0.1f;
	}

	// Deterministic: fixed scale decrement.
	const float step = 0.05f;
	for (float s = max_scale; s >= min_scale - 1e-6f; s -= step) {
		int w = font_measure_text_width(fs, text, s);
		if (w <= max_w) {
			return s;
		}
	}
	return min_scale;
}

static void draw_text_fit(FontSystem* fs, Framebuffer* fb, int x, int y, int max_w, const char* text, ColorRGBA color, const HudTextFit* fit) {
	if (!fs || !fb || !text || !fit) {
		return;
	}
	if (max_w <= 0) {
		return;
	}
	float s = choose_text_scale(fs, text, max_w, fit->min_scale, fit->max_scale);
	if (font_measure_text_width(fs, text, s) <= max_w) {
		font_draw_text(fs, fb, x, y, text, color, s);
		return;
	}

	// Still doesn't fit even at min scale -> ellipsize.
	const char* ell = "...";
	int ell_w = font_measure_text_width(fs, ell, fit->min_scale);
	if (ell_w > max_w) {
		return;
	}

	char tmp[128];
	size_t n = strlen(text);
	if (n >= sizeof(tmp)) {
		n = sizeof(tmp) - 1;
	}
	// Try progressively shorter prefixes.
	for (size_t keep = n; keep > 0; keep--) {
		memcpy(tmp, text, keep);
		tmp[keep] = '\0';
		// Append ellipsis.
		size_t cur = strlen(tmp);
		if (cur + 3 >= sizeof(tmp)) {
			continue;
		}
		strcat(tmp, ell);
		int w = font_measure_text_width(fs, tmp, fit->min_scale);
		if (w <= max_w) {
			font_draw_text(fs, fb, x, y, tmp, color, fit->min_scale);
			return;
		}
	}
	// Nothing fits except maybe the ellipsis.
	font_draw_text(fs, fb, x, y, ell, color, fit->min_scale);
}

static bool hud_system_build(HudSystem* out, const CoreConfig* cfg, const AssetPaths* paths, TextureRegistry* texreg) {
	if (!out || !cfg || !paths) {
		return false;
	}
	memset(out, 0, sizeof(*out));

	// Load HUD JSON.
	if (!hud_asset_load(&out->asset, paths, cfg->ui.hud.file)) {
		return false;
	}
	out->loaded = true;
	strncpy(out->hud_filename, cfg->ui.hud.file, sizeof(out->hud_filename) - 1);
	out->hud_filename[sizeof(out->hud_filename) - 1] = '\0';

	// Load HUD font (UI font by default; can be overridden by the HUD asset).
	const char* font_file = cfg->ui.font.file;
	if (out->asset.panel.text.font_file[0] != '\0') {
		font_file = out->asset.panel.text.font_file;
	}
	if (!font_system_init(&out->font, font_file, cfg->ui.font.size_px, cfg->ui.font.atlas_size, cfg->ui.font.atlas_size, paths)) {
		log_error("HUD: failed to load font: %s", font_file ? font_file : "(null)");
		return false;
	}
	out->font_loaded = true;

	// Cache optional textures used for scaled backgrounds.
	if (texreg && out->asset.bar.background.mode == HUD_BACKGROUND_IMAGE) {
		out->bar_bg_tex = texture_registry_get(texreg, paths, out->asset.bar.background.image);
		if (!out->bar_bg_tex || !out->bar_bg_tex->pixels) {
			log_error("HUD: bar background image not found: %s", out->asset.bar.background.image);
			return false;
		}
	}
	if (texreg && out->asset.panel.background.mode == HUD_BACKGROUND_IMAGE) {
		out->panel_bg_tex = texture_registry_get(texreg, paths, out->asset.panel.background.image);
		if (!out->panel_bg_tex || !out->panel_bg_tex->pixels) {
			log_error("HUD: panel background image not found: %s", out->asset.panel.background.image);
			return false;
		}
	}

	return true;
}

bool hud_system_init(HudSystem* hud, const CoreConfig* cfg, const AssetPaths* paths, TextureRegistry* texreg) {
	if (!hud) {
		return false;
	}
	HudSystem next;
	if (!hud_system_build(&next, cfg, paths, texreg)) {
		hud_system_shutdown(&next);
		return false;
	}
	*hud = next;
	return true;
}

bool hud_system_reload(HudSystem* hud, const CoreConfig* cfg, const AssetPaths* paths, TextureRegistry* texreg) {
	if (!hud) {
		return false;
	}
	HudSystem next;
	if (!hud_system_build(&next, cfg, paths, texreg)) {
		hud_system_shutdown(&next);
		return false;
	}
	// Swap in the new state.
	hud_system_shutdown(hud);
	*hud = next;
	return true;
}

void hud_system_shutdown(HudSystem* hud) {
	if (!hud) {
		return;
	}
	if (hud->font_loaded) {
		font_system_shutdown(&hud->font);
		hud->font_loaded = false;
	}
	hud->loaded = false;
	hud->bar_bg_tex = NULL;
	hud->panel_bg_tex = NULL;
	hud->hud_filename[0] = '\0';
}

static void draw_widget_contents(
	HudSystem* hud,
	Framebuffer* fb,
	int x,
	int y,
	int w,
	int h,
	const Player* player,
	const GameState* state,
	HudWidgetKind kind,
	TextureRegistry* texreg,
	const AssetPaths* paths
) {
	(void)state;
	if (!hud || !hud->font_loaded || !fb || w <= 0 || h <= 0) {
		return;
	}
	FontSystem* font = &hud->font;
	const HudTextStyle* ts = &hud->asset.panel.text;
	int pad_x = ts->padding_x;
	int pad_y = ts->padding_y;
	int cx = x + pad_x;
	int cy = y + pad_y;
	int cw = w - pad_x * 2;
	if (cw <= 0) {
		return;
	}

	ColorRGBA col = color_from_abgr(ts->color_abgr);
	ColorRGBA acc = color_from_abgr(ts->accent_color_abgr);

	const WeaponDef* wdef = player ? weapon_def_get(player->weapon_equipped) : NULL;
	int hp = player ? player->health : 0;
	int hp_max = player ? player->health_max : 0;
	int mortum = player ? player->mortum_pct : 0;
	int ammo_cur = 0;
	int ammo_max = 0;
	if (player && wdef) {
		ammo_cur = ammo_get(&player->ammo, wdef->ammo_type);
		ammo_max = ammo_get_max(&player->ammo, wdef->ammo_type);
	}
	int keys = player ? count_bits_u32((unsigned int)player->keys) : 0;

	char buf0[128];
	char buf1[128];
	buf0[0] = '\0';
	buf1[0] = '\0';

	if (kind == HUD_WIDGET_HEALTH) {
		snprintf(buf0, sizeof(buf0), "HP %d/%d", hp, hp_max);
		draw_text_fit(font, fb, cx, cy, cw, buf0, col, &ts->fit);
		return;
	}
	if (kind == HUD_WIDGET_MORTUM) {
		snprintf(buf0, sizeof(buf0), "MORTUM %d%%", mortum);
		draw_text_fit(font, fb, cx, cy, cw, buf0, acc, &ts->fit);
		if (player && player->undead_active) {
			int lh = font_line_height(font, 1.0f);
			if (pad_y + lh + 2 < h) {
				snprintf(buf1, sizeof(buf1), "UNDEAD %d/%d", player->undead_shards_collected, player->undead_shards_required);
				draw_text_fit(font, fb, cx, cy + lh, cw, buf1, color_from_abgr(0xFFFF9090u), &ts->fit);
			}
		}
		return;
	}
	if (kind == HUD_WIDGET_AMMO) {
		snprintf(buf0, sizeof(buf0), "AMMO %d/%d", ammo_cur, ammo_max);

		int text_w = cw;
		// Optional weapon icon on the right.
		if (player && texreg && paths) {
			const WeaponVisualSpec* spec = weapon_visual_spec_get(player->weapon_equipped);
			if (spec) {
				char filename[128];
				snprintf(filename, sizeof(filename), "Weapons/%s/%s-ICON.png", spec->dir_name, spec->prefix);
				const Texture* icon = texture_registry_get(texreg, paths, filename);
				if (icon && icon->pixels && icon->width > 0 && icon->height > 0) {
					int icon_pad = 6;
					int dst_x = x + w - icon->width - icon_pad;
					int dst_y = y + (h - icon->height) / 2;
					draw_blit_abgr8888_alpha(fb, dst_x, dst_y, icon->pixels, icon->width, icon->height);
					int reserved = (w - (dst_x - x));
					text_w = cw - reserved;
					if (text_w < 0) {
						text_w = 0;
					}
				}
			}
		}

		draw_text_fit(font, fb, cx, cy, text_w, buf0, col, &ts->fit);
		return;
	}
	if (kind == HUD_WIDGET_EQUIPPED_WEAPON) {
		const char* name = wdef ? wdef->name : "(none)";
		snprintf(buf0, sizeof(buf0), "WEAPON %s", name);
		draw_text_fit(font, fb, cx, cy, cw, buf0, col, &ts->fit);
		return;
	}
	if (kind == HUD_WIDGET_KEYS) {
		snprintf(buf0, sizeof(buf0), "KEYS %d", keys);
		draw_text_fit(font, fb, cx, cy, cw, buf0, col, &ts->fit);
		return;
	}
}

void hud_draw(HudSystem* hud, Framebuffer* fb, const Player* player, const GameState* state, int fps, TextureRegistry* texreg, const AssetPaths* paths) {
	(void)fps;
	if (!hud || !hud->loaded || !hud->font_loaded || !fb) {
		return;
	}

	// Doom-style bottom bar height (classic) to preserve overlap invariants.
	int bar_h = fb->height / 5;
	if (bar_h < 40) {
		bar_h = 40;
	}
	if (bar_h > 80) {
		bar_h = 80;
	}
	int bar_y = fb->height - bar_h;

	// Bar background + bevel.
	draw_background(fb, 0, bar_y, fb->width, bar_h, &hud->asset.bar.background, hud->bar_bg_tex);
	draw_bevel(fb, 0, bar_y, fb->width, bar_h, &hud->asset.bar.bevel);

	int n = hud->asset.widget_count;
	if (n <= 0) {
		return;
	}
	int pad = hud->asset.bar.padding_px;
	int gap = hud->asset.bar.gap_px;
	if (pad < 0) {
		pad = 0;
	}
	if (gap < 0) {
		gap = 0;
	}

	int panel_h = bar_h - pad * 2;
	if (panel_h < 20) {
		pad = 2;
		panel_h = bar_h - pad * 2;
	}
	if (panel_h <= 0) {
		return;
	}
	int panel_y = bar_y + pad;

	int total_gap = gap * (n - 1);
	int avail_w = fb->width - pad * 2 - total_gap;
	if (avail_w < n) {
		avail_w = n;
	}
	int base_w = avail_w / n;
	int rem = avail_w - base_w * n;
	if (base_w <= 0) {
		return;
	}

	int x = pad;
	for (int i = 0; i < n; i++) {
		int pw = base_w;
		if (i == n - 1) {
			pw += rem;
		}

		// Shadow (behind panel).
		if (hud->asset.panel.shadow.enabled) {
			draw_rect_abgr8888_alpha(
				fb,
				x + hud->asset.panel.shadow.offset_x,
				panel_y + hud->asset.panel.shadow.offset_y,
				pw,
				panel_h,
				hud->asset.panel.shadow.color_abgr
			);
		}

		// Panel background + bevel.
		draw_background(fb, x, panel_y, pw, panel_h, &hud->asset.panel.background, hud->panel_bg_tex);
		draw_bevel(fb, x, panel_y, pw, panel_h, &hud->asset.panel.bevel);

		// Widget contents.
		draw_widget_contents(hud, fb, x, panel_y, pw, panel_h, player, state, hud->asset.widgets[i].kind, texreg, paths);

		x += pw + gap;
	}

	// Top-left status for win/lose (kept for now).
	if (state && state->mode == GAME_MODE_WIN) {
		font_draw_text(&hud->font, fb, 8, 8, "YOU ESCAPED", color_from_abgr(0xFF90FF90u), 1.0f);
	} else if (state && state->mode == GAME_MODE_LOSE) {
		font_draw_text(&hud->font, fb, 8, 8, "YOU DIED", color_from_abgr(0xFFFF9090u), 1.0f);
	}
}
