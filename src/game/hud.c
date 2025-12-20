#include "game/hud.h"

#include "game/ammo.h"
#include "game/weapon_defs.h"
#include "game/weapon_visuals.h"

#include "render/draw.h"
#include "render/font.h"

#include <stdio.h>

static int count_bits_u32(unsigned int v) {
	int n = 0;
	while (v) {
		n += (int)(v & 1u);
		v >>= 1u;
	}
	return n;
}

static void hud_draw_panel(Framebuffer* fb, int x, int y, int w, int h, uint32_t bg, uint32_t hi, uint32_t lo) {
	draw_rect(fb, x, y, w, h, bg);
	// simple bevel
	draw_rect(fb, x, y, w, 2, hi);
	draw_rect(fb, x, y, 2, h, hi);
	draw_rect(fb, x, y + h - 2, w, 2, lo);
	draw_rect(fb, x + w - 2, y, 2, h, lo);
}

void hud_draw(Framebuffer* fb, const Player* player, const GameState* state, int fps, TextureRegistry* texreg, const AssetPaths* paths) {
	(void)fps;
	const WeaponDef* w = player ? weapon_def_get(player->weapon_equipped) : NULL;
	int hp = player ? player->health : 0;
	int hp_max = player ? player->health_max : 0;
	int mortum = player ? player->mortum_pct : 0;
	int ammo_cur = 0;
	int ammo_max = 0;
	if (player && w) {
		ammo_cur = ammo_get(&player->ammo, w->ammo_type);
		ammo_max = ammo_get_max(&player->ammo, w->ammo_type);
	}
	int keys = player ? count_bits_u32((unsigned int)player->keys) : 0;

	// Doom-style bottom bar
	int bar_h = fb->height / 5;
	if (bar_h < 40) {
		bar_h = 40;
	}
	if (bar_h > 80) {
		bar_h = 80;
	}
	int bar_y = fb->height - bar_h;

	uint32_t bg = 0xFF202020u;
	uint32_t hi = 0xFF404040u;
	uint32_t lo = 0xFF101010u;
	uint32_t text = 0xFFFFFFFFu;
	uint32_t text2 = 0xFFFFE0A0u;

	draw_rect(fb, 0, bar_y, fb->width, bar_h, bg);
	// bevel the whole bar
	draw_rect(fb, 0, bar_y, fb->width, 2, hi);
	draw_rect(fb, 0, bar_y, 2, bar_h, hi);
	draw_rect(fb, 0, bar_y + bar_h - 2, fb->width, 2, lo);
	draw_rect(fb, fb->width - 2, bar_y, 2, bar_h, lo);

	int pad = 8;
	int panel_gap = 6;
	int panel_h = bar_h - pad * 2;
	if (panel_h < 20) {
		panel_h = bar_h - 4;
		pad = 2;
	}
	int panel_y = bar_y + pad;
	int panel_w = (fb->width - pad * 2 - panel_gap * 3) / 4;
	if (panel_w < 40) {
		panel_w = 40;
	}

	int x = pad;
	hud_draw_panel(fb, x, panel_y, panel_w, panel_h, 0xFF282828u, hi, lo);
	char buf[64];
	snprintf(buf, sizeof(buf), "HP %d/%d", hp, hp_max);
	font_draw_text(fb, x + 6, panel_y + 6, buf, text);

	x += panel_w + panel_gap;
	hud_draw_panel(fb, x, panel_y, panel_w, panel_h, 0xFF282828u, hi, lo);
	snprintf(buf, sizeof(buf), "MORTUM %d%%", mortum);
	font_draw_text(fb, x + 6, panel_y + 6, buf, text2);
	if (player && player->undead_active && panel_h >= 28) {
		snprintf(buf, sizeof(buf), "UNDEAD %d/%d", player->undead_shards_collected, player->undead_shards_required);
		font_draw_text(fb, x + 6, panel_y + 20, buf, 0xFFFF9090u);
	}

	x += panel_w + panel_gap;
	hud_draw_panel(fb, x, panel_y, panel_w, panel_h, 0xFF282828u, hi, lo);
	snprintf(buf, sizeof(buf), "AMMO %d/%d", ammo_cur, ammo_max);
	font_draw_text(fb, x + 6, panel_y + 6, buf, text);

	// Weapon icon (optional): draw inside the ammo panel.
	if (player && texreg && paths) {
		const WeaponVisualSpec* spec = weapon_visual_spec_get(player->weapon_equipped);
		if (spec) {
			char filename[128];
			snprintf(filename, sizeof(filename), "Weapons/%s/%s-ICON.png", spec->dir_name, spec->prefix);
			const Texture* icon = texture_registry_get(texreg, paths, filename);
			if (icon && icon->pixels && icon->width > 0 && icon->height > 0) {
				int icon_pad = 6;
				int dst_x = x + panel_w - icon->width - icon_pad;
				int dst_y = panel_y + (panel_h - icon->height) / 2;
				draw_blit_abgr8888_alpha(fb, dst_x, dst_y, icon->pixels, icon->width, icon->height);
			}
		}
	}

	x += panel_w + panel_gap;
	hud_draw_panel(fb, x, panel_y, panel_w, panel_h, 0xFF282828u, hi, lo);
	snprintf(buf, sizeof(buf), "KEYS %d", keys);
	font_draw_text(fb, x + 6, panel_y + 6, buf, text);

	if (state && state->mode == GAME_MODE_WIN) {
		font_draw_text(fb, 8, 8, "YOU ESCAPED", 0xFF90FF90u);
	} else if (state && state->mode == GAME_MODE_LOSE) {
		font_draw_text(fb, 8, 8, "YOU DIED", 0xFFFF9090u);
	}
}
