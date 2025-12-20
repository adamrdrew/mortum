#include "game/hud.h"

#include "game/ammo.h"
#include "game/weapon_defs.h"
#include "render/font.h"

#include <stdio.h>

void hud_draw(Framebuffer* fb, const Player* player, const GameState* state, int fps) {
	const WeaponDef* w = player ? weapon_def_get(player->weapon_equipped) : NULL;
	int hp = player ? player->health : 0;
	int hp_max = player ? player->health_max : 0;
	int b = player ? ammo_get(&player->ammo, AMMO_BULLETS) : 0;
	int bmax = player ? ammo_get_max(&player->ammo, AMMO_BULLETS) : 0;
	int s = player ? ammo_get(&player->ammo, AMMO_SHELLS) : 0;
	int smax = player ? ammo_get_max(&player->ammo, AMMO_SHELLS) : 0;
	int c = player ? ammo_get(&player->ammo, AMMO_CELLS) : 0;
	int cmax = player ? ammo_get_max(&player->ammo, AMMO_CELLS) : 0;

	char line1[128];
	snprintf(
		line1,
		sizeof(line1),
		"HP: %d/%d  Wpn: %s  B:%d/%d S:%d/%d C:%d/%d  Purge:%d  Key:%s",
		hp,
		hp_max,
		w ? w->name : "-",
		b,
		bmax,
		s,
		smax,
		c,
		cmax,
		player ? player->purge_items : 0,
		(player && player->keys != 0) ? "yes" : "no");
	font_draw_text(fb, 8, 8, line1, 0xFFFFFFFFu);

	char line2[128];
	if (player && player->undead_active) {
		snprintf(
			line2,
			sizeof(line2),
			"UNDEAD  Shards: %d/%d  Mortum: %d%%  FPS: %d",
			player->undead_shards_collected,
			player->undead_shards_required,
			player->mortum_pct,
			fps);
	} else {
		snprintf(line2, sizeof(line2), "Mortum: %d%%  FPS: %d", player ? player->mortum_pct : 0, fps);
	}
	font_draw_text(fb, 8, 24, line2, 0xFFFFE0A0u);

	if (state && state->mode == GAME_MODE_WIN) {
		font_draw_text(fb, 8, 48, "YOU ESCAPED", 0xFF90FF90u);
	} else if (state && state->mode == GAME_MODE_LOSE) {
		font_draw_text(fb, 8, 48, "YOU DIED", 0xFFFF9090u);
	}
}
