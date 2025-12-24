#include "game/debug_overlay.h"

#include "game/font.h"

#include <stdio.h>

static inline ColorRGBA color_from_abgr(uint32_t abgr) {
	ColorRGBA c;
	c.a = (uint8_t)((abgr >> 24) & 0xFFu);
	c.b = (uint8_t)((abgr >> 16) & 0xFFu);
	c.g = (uint8_t)((abgr >> 8) & 0xFFu);
	c.r = (uint8_t)(abgr & 0xFFu);
	return c;
}

void debug_overlay_draw(FontSystem* font, Framebuffer* fb, const Player* player, const World* world, const EntitySystem* entities, int fps) {
	if (!fb || !player) {
		return;
	}
	if (!font) {
		return;
	}

	int sector_id = -1;
	if (world && (unsigned)player->body.sector < (unsigned)world->sector_count) {
		sector_id = world->sectors[player->body.sector].id;
	}

	char line[160];
	uint32_t ecount = entities ? entity_system_alive_count(entities) : 0u;
	snprintf(line, sizeof(line), "DBG  Pos: %.2f,%.2f,%.2f  Ang: %.1f  Sector: %d  Mortum: %d%%  Ent: %u  FPS: %d", player->body.x, player->body.y, player->body.z, player->angle_deg, sector_id, player->mortum_pct, (unsigned)ecount, fps);
	font_draw_text(font, fb, 8, 64, line, color_from_abgr(0xFFA0FFA0u), 1.0f);

	if (entities && entities->defs && entities->defs->defs && ecount > 0) {
		const Entity* first = NULL;
		for (uint32_t i = 0; i < entities->capacity; i++) {
			if (entities->alive[i]) {
				first = &entities->entities[i];
				break;
			}
		}
		if (first && (uint32_t)first->def_id < entities->defs->count) {
			const EntityDef* def = &entities->defs->defs[first->def_id];
			const char* spr = (def->sprite.file.name[0] != '\0') ? def->sprite.file.name : "(none)";
			snprintf(line, sizeof(line), "E0  def=%s  spr=%s  pos=%.2f,%.2f,%.2f", def->name, spr, first->body.x, first->body.y, first->body.z);
			font_draw_text(font, fb, 8, 72, line, color_from_abgr(0xFF90E0FFu), 1.0f);
		}
	}
}
