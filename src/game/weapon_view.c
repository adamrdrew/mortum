#include "game/weapon_view.h"

#include "game/weapon_visuals.h"

#include "render/draw.h"

#include <math.h>
#include <stdio.h>

static int clampi(int v, int lo, int hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

void weapon_view_draw(Framebuffer* fb, const Player* player, TextureRegistry* texreg, const AssetPaths* paths) {
	if (!fb || !player || !texreg || !paths) {
		return;
	}

	const WeaponVisualSpec* spec = weapon_visual_spec_get(player->weapon_equipped);
	if (!spec) {
		return;
	}

	char filename[128];
	if (player->weapon_view_anim_shooting) {
		int frame = player->weapon_view_anim_frame;
		if (frame < 0) {
			frame = 0;
		}
		if (frame > 5) {
			frame = 5;
		}
		snprintf(filename, sizeof(filename), "Weapons/%s/%s-SHOOT-%d.png", spec->dir_name, spec->prefix, frame + 1);
	} else {
		snprintf(filename, sizeof(filename), "Weapons/%s/%s-IDLE.png", spec->dir_name, spec->prefix);
	}
	const Texture* t = texture_registry_get(texreg, paths, filename);
	if (!t || !t->pixels || t->width <= 0 || t->height <= 0) {
		return;
	}

	int bar_h = fb->height / 5;
	bar_h = clampi(bar_h, 40, 80);
	int bar_y = fb->height - bar_h;

	int overlap_px = 6;
	float amp = player->weapon_view_bob_amp;
	float phase = player->weapon_view_bob_phase;
	int bob_x = (int)lroundf(sinf(phase) * amp * 6.0f);
	int bob_y = (int)lroundf(fabsf(cosf(phase)) * amp * 4.0f);

	int dst_x = (fb->width - t->width) / 2 + bob_x;
	int dst_y = bar_y - t->height + overlap_px + bob_y;

	draw_blit_abgr8888_alpha(fb, dst_x, dst_y, t->pixels, t->width, t->height);
}
