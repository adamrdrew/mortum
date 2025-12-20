#include "game/debug_overlay.h"

#include "render/font.h"

#include <stdio.h>

void debug_overlay_draw(Framebuffer* fb, const Player* player, const World* world, int fps) {
	if (!fb || !player) {
		return;
	}

	int sector_id = -1;
	if (world && world->sector_count > 0) {
		sector_id = world->sectors[0].id;
	}

	char line[160];
	snprintf(line, sizeof(line), "DBG  Pos: %.2f,%.2f  Ang: %.1f  Sector: %d  Mortum: %d%%  FPS: %d", player->x, player->y, player->angle_deg, sector_id, player->mortum_pct, fps);
	font_draw_text(fb, 8, 64, line, 0xFFA0FFA0u);
}
