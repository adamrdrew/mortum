#include "game/debug_overlay.h"

#include "render/font.h"

#include <stdio.h>

void debug_overlay_draw(Framebuffer* fb, const Player* player, const World* world, const EntitySystem* entities, int fps) {
	if (!fb || !player) {
		return;
	}

	int sector_id = -1;
	if (world && (unsigned)player->body.sector < (unsigned)world->sector_count) {
		sector_id = world->sectors[player->body.sector].id;
	}

	char line[160];
	uint32_t ecount = entities ? entity_system_alive_count(entities) : 0u;
	snprintf(line, sizeof(line), "DBG  Pos: %.2f,%.2f,%.2f  Ang: %.1f  Sector: %d  Mortum: %d%%  Ent: %u  FPS: %d", player->body.x, player->body.y, player->body.z, player->angle_deg, sector_id, player->mortum_pct, (unsigned)ecount, fps);
	font_draw_text(fb, 8, 64, line, 0xFFA0FFA0u);

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
			font_draw_text(fb, 8, 72, line, 0xFF90E0FFu);
		}
	}
}
