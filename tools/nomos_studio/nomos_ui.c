// Nomos Studio - UI Implementation
#include "nomos_ui.h"
#include "nomos.h"
#include "nomos_document.h"
#include "nomos_font.h"

#include "game/entities.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

// External font from nomos_main.c
extern NomosFont g_nomos_font;

static void draw_rect(SDL_Renderer* r, SDL_Rect rect, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	SDL_RenderFillRect(r, &rect);
}

static void draw_rect_outline(SDL_Renderer* r, SDL_Rect rect, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	SDL_RenderDrawRect(r, &rect);
}

static void draw_line(SDL_Renderer* r, int x1, int y1, int x2, int y2, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
	SDL_SetRenderDrawColor(r, cr, cg, cb, ca);
	SDL_RenderDrawLine(r, x1, y1, x2, y2);
}

// Text rendering using the Nomos font system
static void draw_text(SDL_Renderer* r, int x, int y, const char* text, Uint8 cr, Uint8 cg, Uint8 cb, Uint8 ca) {
	if (!text || !text[0]) return;
	nomos_font_draw(&g_nomos_font, r, x, y, text, cr, cg, cb, ca);
}

static int text_width(const char* text) {
	if (!text) return 0;
	return nomos_font_measure_width(&g_nomos_font, text);
}

static bool point_in_rect(int x, int y, SDL_Rect rect) {
	return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
}

// UI lifecycle
void nomos_ui_init(NomosUI* ui, SDL_Renderer* renderer) {
	if (!ui) return;
	memset(ui, 0, sizeof(*ui));
	ui->renderer = renderer;
	ui->open_menu = -1;
	ui->palette_mode = NOMOS_PALETTE_ENTITIES;
	ui->palette_hovered = -1;
	ui->palette_selected = -1;
}

void nomos_ui_destroy(NomosUI* ui) {
	if (!ui) return;
	memset(ui, 0, sizeof(*ui));
}

void nomos_ui_calculate_layout(NomosLayout* layout, int window_width, int window_height) {
	if (!layout) return;
	
	// Get scale factor from font (which has HiDPI scale)
	float scale = g_nomos_font.ui_scale;
	if (scale < 1.0f) scale = 1.0f;
	
	int menu_h = (int)(NOMOS_MENU_HEIGHT * scale);
	int status_h = (int)(NOMOS_STATUS_HEIGHT * scale);
	int left_w = (int)(NOMOS_LEFT_PANEL_WIDTH * scale);
	int right_w = (int)(NOMOS_RIGHT_PANEL_WIDTH * scale);
	
	layout->menu_bar.x = 0;
	layout->menu_bar.y = 0;
	layout->menu_bar.w = window_width;
	layout->menu_bar.h = menu_h;
	
	layout->status_bar.x = 0;
	layout->status_bar.y = window_height - status_h;
	layout->status_bar.w = window_width;
	layout->status_bar.h = status_h;
	
	layout->left_panel.x = 0;
	layout->left_panel.y = menu_h;
	layout->left_panel.w = left_w;
	layout->left_panel.h = window_height - menu_h - status_h;
	
	layout->right_panel.x = window_width - right_w;
	layout->right_panel.y = menu_h;
	layout->right_panel.w = right_w;
	layout->right_panel.h = window_height - menu_h - status_h;
	
	layout->viewport.x = left_w;
	layout->viewport.y = menu_h;
	layout->viewport.w = window_width - left_w - right_w;
	layout->viewport.h = window_height - menu_h - status_h;
}

// Event handling
bool nomos_ui_handle_event(NomosUI* ui, SDL_Event* event, NomosDocument* doc __attribute__((unused)), NomosDialogState* dialog) {
	if (!ui || !event) return false;
	
	// Dialog takes priority
	if (dialog && dialog->type != NOMOS_DIALOG_NONE) {
		return nomos_dialog_handle_event(dialog, event);
	}
	
	// Get UI scale for converting logical mouse coords to render coords
	float scale = g_nomos_font.ui_scale;
	if (scale < 1.0f) scale = 1.0f;
	
	switch (event->type) {
		case SDL_MOUSEMOTION:
			ui->mouse_x = (int)(event->motion.x * scale);
			ui->mouse_y = (int)(event->motion.y * scale);
			break;
			
		case SDL_MOUSEBUTTONDOWN:
			if (event->button.button == SDL_BUTTON_LEFT) {
				ui->mouse_down = true;
				ui->mouse_clicked = true;
			}
			break;
			
		case SDL_MOUSEBUTTONUP:
			if (event->button.button == SDL_BUTTON_LEFT) {
				ui->mouse_down = false;
			}
			break;
			
		case SDL_MOUSEWHEEL:
			// Handle scrolling in panels
			break;
			
		default:
			break;
	}
	
	return false; // Let other systems handle the event too
}

// Menu bar rendering (just the bar, not dropdowns)
static NomosMenuAction render_menu_bar(NomosUI* ui, SDL_Renderer* r, SDL_Rect rect) {
	NomosMenuAction result = NOMOS_MENU_NONE;
	
	// Background
	draw_rect(r, rect, NOMOS_COLOR_BG_PANEL);
	
	// Get scale factor for HiDPI
	float scale = g_nomos_font.ui_scale;
	if (scale < 1.0f) scale = 1.0f;
	
	// Menu items - use scaled widths
	const char* menus[] = {"File", "Generate", "Run"};
	int menu_widths[3];
	int menu_x = (int)(8 * scale);
	
	// Calculate menu widths based on text
	for (int i = 0; i < 3; i++) {
		menu_widths[i] = text_width(menus[i]) + (int)(16 * scale);
	}
	
	int current_x = menu_x;
	for (int i = 0; i < 3; i++) {
		SDL_Rect item_rect = {current_x, rect.y, menu_widths[i], rect.h};
		bool hovered = point_in_rect(ui->mouse_x, ui->mouse_y, item_rect);
		
		if (hovered || ui->open_menu == i) {
			draw_rect(r, item_rect, NOMOS_COLOR_BG_BUTTON_HOV);
		}
		
		draw_text(r, item_rect.x + (int)(8 * scale), item_rect.y + (int)(6 * scale), menus[i], NOMOS_COLOR_TEXT);
		
		if (hovered && ui->mouse_clicked) {
			ui->open_menu = (ui->open_menu == i) ? -1 : i;
			ui->menu_active = true;
		}
		
		current_x += menu_widths[i];
	}
	
	// Close menu when clicking outside (check before dropdown renders)
	if (ui->mouse_clicked && ui->open_menu >= 0) {
		// Calculate dropdown position for hit testing
		int dropdown_x = (int)(8 * scale);
		for (int i = 0; i < ui->open_menu; i++) {
			dropdown_x += menu_widths[i];
		}
		int dropdown_y = rect.y + rect.h;
		int dropdown_w = (int)(150 * scale);  // Generous size for hit test
		int dropdown_h = (int)(5 * 24 * scale); // max items
		SDL_Rect dropdown = {dropdown_x, dropdown_y, dropdown_w, dropdown_h};
		
		// Calculate full menu bar area
		int total_menu_w = 0;
		for (int i = 0; i < 3; i++) total_menu_w += menu_widths[i];
		SDL_Rect menu_area = {(int)(8 * scale), rect.y, total_menu_w, rect.h};
		
		if (!point_in_rect(ui->mouse_x, ui->mouse_y, dropdown) &&
			!point_in_rect(ui->mouse_x, ui->mouse_y, menu_area)) {
			ui->open_menu = -1;
			ui->menu_active = false;
		}
	}
	
	return result;
}

// Render dropdown menus on top of everything else
static NomosMenuAction render_menu_dropdowns(NomosUI* ui, SDL_Renderer* r, SDL_Rect menu_bar_rect) {
	NomosMenuAction result = NOMOS_MENU_NONE;
	
	if (ui->open_menu < 0) return result;
	
	// Get scale factor for HiDPI
	float scale = g_nomos_font.ui_scale;
	if (scale < 1.0f) scale = 1.0f;
	
	// Recalculate menu widths
	const char* menus[] = {"File", "Generate", "Run"};
	int menu_widths[3];
	for (int i = 0; i < 3; i++) {
		menu_widths[i] = text_width(menus[i]) + (int)(16 * scale);
	}
	
	// Calculate dropdown X by summing previous menu widths
	int dropdown_x = (int)(8 * scale);
	for (int i = 0; i < ui->open_menu; i++) {
		dropdown_x += menu_widths[i];
	}
	int dropdown_y = menu_bar_rect.y + menu_bar_rect.h;
	int item_h = (int)(24 * scale);
	
	const char* file_items[] = {"Open...", "Save", "Save As...", "Validate", "Exit"};
	NomosMenuAction file_actions[] = {NOMOS_MENU_OPEN, NOMOS_MENU_SAVE, NOMOS_MENU_SAVE_AS, NOMOS_MENU_VALIDATE, NOMOS_MENU_EXIT};
	const char* gen_items[] = {"Generate..."};
	NomosMenuAction gen_actions[] = {NOMOS_MENU_GENERATE};
	const char* run_items[] = {"Run in Mortum"};
	NomosMenuAction run_actions[] = {NOMOS_MENU_RUN};
	
	const char** items = NULL;
	NomosMenuAction* actions = NULL;
	int item_count = 0;
	
	switch (ui->open_menu) {
		case 0:
			items = file_items;
			actions = file_actions;
			item_count = 5;
			break;
		case 1:
			items = gen_items;
			actions = gen_actions;
			item_count = 1;
			break;
		case 2:
			items = run_items;
			actions = run_actions;
			item_count = 1;
			break;
	}
	
	if (items) {
		// Calculate dropdown width based on longest item
		int dropdown_w = 0;
		for (int i = 0; i < item_count; i++) {
			int tw = text_width(items[i]) + (int)(24 * scale);
			if (tw > dropdown_w) dropdown_w = tw;
		}
		
		SDL_Rect dropdown = {dropdown_x, dropdown_y, dropdown_w, item_count * item_h};
		draw_rect(r, dropdown, NOMOS_COLOR_BG_PANEL);
		draw_rect_outline(r, dropdown, NOMOS_COLOR_BORDER);
		
		for (int i = 0; i < item_count; i++) {
			SDL_Rect item_rect = {dropdown_x, dropdown_y + i * item_h, dropdown_w, item_h};
			bool hovered = point_in_rect(ui->mouse_x, ui->mouse_y, item_rect);
			
			if (hovered) {
				draw_rect(r, item_rect, NOMOS_COLOR_BG_BUTTON_HOV);
				if (ui->mouse_clicked) {
					result = actions[i];
					ui->open_menu = -1;
					ui->menu_active = false;
				}
			}
			
			draw_text(r, item_rect.x + (int)(8 * scale), item_rect.y + (int)(6 * scale), items[i], NOMOS_COLOR_TEXT);
		}
	}
	
	return result;
}

// Left panel (palette)
static void render_left_panel(NomosUI* ui, SDL_Renderer* r, SDL_Rect rect, 
	NomosDocument* doc, const EntityDefs* entity_defs) {
	(void)doc;
	
	// Background
	draw_rect(r, rect, NOMOS_COLOR_BG_PANEL);
	draw_line(r, rect.x + rect.w - 1, rect.y, rect.x + rect.w - 1, rect.y + rect.h, NOMOS_COLOR_BORDER);
	
	// Mode tabs
	int tab_y = rect.y + 4;
	int tab_h = 20;
	const char* tabs[] = {"Entities", "Lights", "Particles", "Player"};
	
	for (int i = 0; i < 4; i++) {
		SDL_Rect tab_rect = {rect.x + 4, tab_y + i * (tab_h + 2), rect.w - 8, tab_h};
		bool selected = (ui->palette_mode == (NomosPaletteMode)i);
		bool hovered = point_in_rect(ui->mouse_x, ui->mouse_y, tab_rect);
		
		if (selected) {
			draw_rect(r, tab_rect, NOMOS_COLOR_BG_BUTTON_ACT);
		} else if (hovered) {
			draw_rect(r, tab_rect, NOMOS_COLOR_BG_BUTTON_HOV);
		} else {
			draw_rect(r, tab_rect, NOMOS_COLOR_BG_BUTTON);
		}
		
		draw_text(r, tab_rect.x + 4, tab_rect.y + 4, tabs[i], NOMOS_COLOR_TEXT);
		
		if (hovered && ui->mouse_clicked) {
			ui->palette_mode = (NomosPaletteMode)i;
			ui->palette_scroll = 0;
			ui->palette_selected = -1;
		}
	}
	
	// Content area
	int content_y = tab_y + 4 * (tab_h + 2) + 8;
	int content_h = rect.h - (content_y - rect.y) - 4;
	SDL_Rect content = {rect.x + 4, content_y, rect.w - 8, content_h};
	
	// Draw content based on mode
	if (ui->palette_mode == NOMOS_PALETTE_ENTITIES && entity_defs) {
		// Entity list
		int item_h = 24;
		int visible_count = content_h / item_h;
		(void)visible_count;
		
		for (uint32_t i = 0; i < entity_defs->count && (int)i * item_h < content_h; i++) {
			SDL_Rect item = {content.x, content.y + (int)i * item_h - ui->palette_scroll, content.w, item_h - 2};
			
			if (item.y + item.h < content.y || item.y > content.y + content.h) continue;
			
			bool hovered = point_in_rect(ui->mouse_x, ui->mouse_y, item);
			bool selected = (ui->palette_selected == (int)i);
			
			if (selected) {
				draw_rect(r, item, NOMOS_COLOR_SELECTED);
			} else if (hovered) {
				draw_rect(r, item, NOMOS_COLOR_BG_BUTTON_HOV);
			}
			
			// Thumbnail placeholder (colored square based on entity kind)
			SDL_Rect thumb = {item.x + 2, item.y + 2, item_h - 6, item_h - 6};
			switch (entity_defs->defs[i].kind) {
				case ENTITY_KIND_PICKUP:
					draw_rect(r, thumb, 100, 200, 100, 255);
					break;
				case ENTITY_KIND_ENEMY:
					draw_rect(r, thumb, 200, 100, 100, 255);
					break;
				case ENTITY_KIND_PROJECTILE:
					draw_rect(r, thumb, 200, 200, 100, 255);
					break;
				default:
					draw_rect(r, thumb, 150, 150, 150, 255);
					break;
			}
			
			draw_text(r, item.x + item_h, item.y + 6, entity_defs->defs[i].name, NOMOS_COLOR_TEXT);
			
			if (hovered && ui->mouse_clicked) {
				ui->palette_selected = (int)i;
			}
		}
	} else if (ui->palette_mode == NOMOS_PALETTE_LIGHTS) {
		draw_text(r, content.x + 4, content.y + 4, "Click in viewport", NOMOS_COLOR_TEXT_DIM);
		draw_text(r, content.x + 4, content.y + 20, "to place light", NOMOS_COLOR_TEXT_DIM);
	} else if (ui->palette_mode == NOMOS_PALETTE_PARTICLES) {
		draw_text(r, content.x + 4, content.y + 4, "Click in viewport", NOMOS_COLOR_TEXT_DIM);
		draw_text(r, content.x + 4, content.y + 20, "to place emitter", NOMOS_COLOR_TEXT_DIM);
	} else if (ui->palette_mode == NOMOS_PALETTE_PLAYER_START) {
		draw_text(r, content.x + 4, content.y + 4, "Click in viewport", NOMOS_COLOR_TEXT_DIM);
		draw_text(r, content.x + 4, content.y + 20, "to move start", NOMOS_COLOR_TEXT_DIM);
	}
}

// Right panel (inspector)
static void render_right_panel(NomosUI* ui, SDL_Renderer* r, SDL_Rect rect,
	NomosDocument* doc, const NomosTextureList* textures, NomosDialogState* dialog) {
	(void)ui;
	(void)textures;
	(void)dialog;
	
	// Background
	draw_rect(r, rect, NOMOS_COLOR_BG_PANEL);
	draw_line(r, rect.x, rect.y, rect.x, rect.y + rect.h, NOMOS_COLOR_BORDER);
	
	int y = rect.y + 8;
	int label_x = rect.x + 8;
	int value_x = rect.x + 80;
	int line_h = 18;
	
	// Title
	const char* title = "Inspector";
	if (doc && doc->has_map) {
		switch (doc->selection.type) {
			case NOMOS_SEL_SECTOR: title = "Sector"; break;
			case NOMOS_SEL_WALL: title = "Wall"; break;
			case NOMOS_SEL_ENTITY: title = "Entity"; break;
			case NOMOS_SEL_LIGHT: title = "Light"; break;
			case NOMOS_SEL_PARTICLE: title = "Particle"; break;
			case NOMOS_SEL_PLAYER_START: title = "Player Start"; break;
			default: break;
		}
	}
	
	draw_text(r, label_x, y, title, NOMOS_COLOR_TEXT);
	y += line_h + 4;
	draw_line(r, rect.x + 4, y, rect.x + rect.w - 4, y, NOMOS_COLOR_BORDER);
	y += 8;
	
	if (!doc || !doc->has_map) {
		draw_text(r, label_x, y, "No map loaded", NOMOS_COLOR_TEXT_DIM);
		return;
	}
	
	// Show properties based on selection
	switch (doc->selection.type) {
		case NOMOS_SEL_SECTOR: {
			int idx = doc->selection.index;
			if (idx >= 0 && idx < doc->map.world.sector_count) {
				Sector* s = &doc->map.world.sectors[idx];
				char buf[64];
				
				snprintf(buf, sizeof(buf), "Sector #%d", s->id);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT);
				y += line_h + 4;
				
				// Floor Z - editable slider
				{
					SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
					int floor_val = (int)(s->floor_z * 10.0f);
					NomosWidgetResult res = nomos_ui_slider_int(ui, r, slider_rect, "Floor Z", &floor_val, -50, 50);
					if (res.value_changed) {
						nomos_document_set_sector_floor_z(doc, idx, (float)floor_val / 10.0f);
					}
					y += 32;
				}
				
				// Ceil Z - editable slider
				{
					SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
					int ceil_val = (int)(s->ceil_z * 10.0f);
					NomosWidgetResult res = nomos_ui_slider_int(ui, r, slider_rect, "Ceil Z", &ceil_val, 10, 100);
					if (res.value_changed) {
						nomos_document_set_sector_ceil_z(doc, idx, (float)ceil_val / 10.0f);
					}
					y += 32;
				}
				
				// Light - editable slider
				{
					SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
					NomosWidgetResult res = nomos_ui_slider_float(ui, r, slider_rect, "Light", &s->light, 0.0f, 1.0f);
					if (res.value_changed) {
						doc->dirty = true;
					}
					y += 32;
				}
				
				draw_text(r, label_x, y, "Floor:", NOMOS_COLOR_TEXT_DIM);
				draw_text(r, value_x, y, s->floor_tex, NOMOS_COLOR_TEXT);
				y += line_h;
				
				draw_text(r, label_x, y, "Ceil:", NOMOS_COLOR_TEXT_DIM);
				draw_text(r, value_x, y, s->ceil_tex, NOMOS_COLOR_TEXT);
				y += line_h;
			}
			break;
		}
		
		case NOMOS_SEL_WALL: {
			int idx = doc->selection.index;
			if (idx >= 0 && idx < doc->map.world.wall_count) {
				Wall* w = &doc->map.world.walls[idx];
				char buf[64];
				
				snprintf(buf, sizeof(buf), "v0: %d  v1: %d", w->v0, w->v1);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT);
				y += line_h;
				
				snprintf(buf, sizeof(buf), "Front: %d", w->front_sector);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT);
				y += line_h;
				
				snprintf(buf, sizeof(buf), "Back: %d", w->back_sector);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT);
				y += line_h;
				
				draw_text(r, label_x, y, "Tex:", NOMOS_COLOR_TEXT_DIM);
				draw_text(r, value_x, y, w->tex, NOMOS_COLOR_TEXT);
				y += line_h;
				
				if (w->end_level) {
					draw_text(r, label_x, y, "[END LEVEL]", NOMOS_COLOR_ACCENT);
					y += line_h;
				}
				
				if (w->back_sector >= 0) {
					draw_text(r, label_x, y, "[PORTAL]", NOMOS_COLOR_WALL_PORTAL);
					y += line_h;
				}
			}
			break;
		}
		
		case NOMOS_SEL_ENTITY: {
			int idx = doc->selection.index;
			if (idx >= 0 && idx < doc->map.entity_count) {
				MapEntityPlacement* e = &doc->map.entities[idx];
				char buf[64];
				
				draw_text(r, label_x, y, "Entity", NOMOS_COLOR_TEXT);
				y += line_h;
				
				draw_text(r, label_x, y, "Type:", NOMOS_COLOR_TEXT_DIM);
				draw_text(r, value_x - 20, y, e->def_name, NOMOS_COLOR_TEXT);
				y += line_h + 4;
				
				snprintf(buf, sizeof(buf), "Pos: %.1f, %.1f", e->x, e->y);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT_DIM);
				y += line_h;
				
				snprintf(buf, sizeof(buf), "Sector: %d", e->sector);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT_DIM);
				y += line_h + 4;
				
				// Yaw - editable slider
				{
					SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
					NomosWidgetResult res = nomos_ui_slider_float(ui, r, slider_rect, "Yaw (deg)", &e->yaw_deg, 0.0f, 360.0f);
					if (res.value_changed) {
						doc->dirty = true;
					}
					y += 32;
				}
				
				// Delete button
				y += 8;
				SDL_Rect del_btn = {label_x, y, 80, 24};
				NomosWidgetResult del_res = nomos_ui_button(ui, r, del_btn, "Delete");
				if (del_res.clicked) {
					nomos_document_remove_entity(doc, idx);
					nomos_document_deselect_all(doc);
				}
			}
			break;
		}
		
		case NOMOS_SEL_LIGHT: {
			int idx = doc->selection.index;
			if (idx >= 0 && idx < doc->map.world.light_count && 
				doc->map.world.light_alive && doc->map.world.light_alive[idx]) {
				PointLight* l = &doc->map.world.lights[idx];
				char buf[64];
				
				snprintf(buf, sizeof(buf), "Light #%d", idx);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT);
				y += line_h + 4;
				
				snprintf(buf, sizeof(buf), "Pos: %.1f, %.1f", l->x, l->y);
				draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT_DIM);
				y += line_h;
				
				// Z height - editable slider
				{
					SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
					NomosWidgetResult res = nomos_ui_slider_float(ui, r, slider_rect, "Height (Z)", &l->z, 0.0f, 10.0f);
					if (res.value_changed) {
						doc->dirty = true;
					}
					y += 32;
				}
				
				// Radius - editable slider
				{
					SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
					NomosWidgetResult res = nomos_ui_slider_float(ui, r, slider_rect, "Radius", &l->radius, 1.0f, 20.0f);
					if (res.value_changed) {
						doc->dirty = true;
					}
					y += 32;
				}
				
				// Intensity - editable slider
				{
					SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
					NomosWidgetResult res = nomos_ui_slider_float(ui, r, slider_rect, "Intensity", &l->intensity, 0.0f, 2.0f);
					if (res.value_changed) {
						doc->dirty = true;
					}
					y += 32;
				}
				
				// Delete button
				y += 8;
				SDL_Rect del_btn = {label_x, y, 80, 24};
				NomosWidgetResult del_res = nomos_ui_button(ui, r, del_btn, "Delete");
				if (del_res.clicked) {
					nomos_document_remove_light(doc, idx);
					nomos_document_deselect_all(doc);
				}
			}
			break;
		}
		
		case NOMOS_SEL_PLAYER_START: {
			char buf[64];
			
			draw_text(r, label_x, y, "Player Start", NOMOS_COLOR_TEXT);
			y += line_h + 4;
			
			snprintf(buf, sizeof(buf), "Pos: %.1f, %.1f", doc->map.player_start_x, doc->map.player_start_y);
			draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT_DIM);
			y += line_h + 4;
			
			// Angle - editable slider
			{
				SDL_Rect slider_rect = {label_x, y, rect.w - 16, 28};
				NomosWidgetResult res = nomos_ui_slider_float(ui, r, slider_rect, "Angle (deg)", &doc->map.player_start_angle_deg, 0.0f, 360.0f);
				if (res.value_changed) {
					doc->dirty = true;
				}
				y += 32;
			}
			break;
		}
		
		default:
			draw_text(r, label_x, y, "No selection", NOMOS_COLOR_TEXT_DIM);
			break;
	}
	
	// Validation results section
	if (doc->has_validation) {
		y = rect.y + rect.h - 150;
		draw_line(r, rect.x + 4, y, rect.x + rect.w - 4, y, NOMOS_COLOR_BORDER);
		y += 8;
		
		char buf[64];
		snprintf(buf, sizeof(buf), "Validation (%d E, %d W)",
			doc->validation.error_count, doc->validation.warning_count);
		draw_text(r, label_x, y, buf, NOMOS_COLOR_TEXT);
		y += line_h + 4;
		
		// Show errors
		for (int i = 0; i < doc->validation.error_count && y < rect.y + rect.h - 20; i++) {
			draw_text(r, label_x, y, "E:", NOMOS_COLOR_ERROR);
			// Truncate message if too long
			char msg[32];
			strncpy(msg, doc->validation.errors[i].message, sizeof(msg) - 1);
			msg[sizeof(msg) - 1] = '\0';
			draw_text(r, label_x + 20, y, msg, NOMOS_COLOR_ERROR);
			y += line_h;
		}
		
		// Show warnings
		for (int i = 0; i < doc->validation.warning_count && y < rect.y + rect.h - 20; i++) {
			draw_text(r, label_x, y, "W:", NOMOS_COLOR_WARNING);
			char msg[32];
			strncpy(msg, doc->validation.warnings[i].message, sizeof(msg) - 1);
			msg[sizeof(msg) - 1] = '\0';
			draw_text(r, label_x + 20, y, msg, NOMOS_COLOR_WARNING);
			y += line_h;
		}
	}
}

// Status bar
static void render_status_bar(SDL_Renderer* r, SDL_Rect rect, NomosDocument* doc) {
	draw_rect(r, rect, NOMOS_COLOR_BG_PANEL);
	draw_line(r, rect.x, rect.y, rect.x + rect.w, rect.y, NOMOS_COLOR_BORDER);
	
	char status[256] = "No map loaded";
	if (doc && doc->has_map) {
		snprintf(status, sizeof(status), "%s%s  |  %d sectors, %d walls, %d entities",
			doc->file_path[0] ? doc->file_path : "Untitled",
			doc->dirty ? " *" : "",
			doc->map.world.sector_count,
			doc->map.world.wall_count,
			doc->map.entity_count);
	}
	
	draw_text(r, rect.x + 8, rect.y + 5, status, NOMOS_COLOR_TEXT_DIM);
}

// Main render function
void nomos_ui_render(NomosUI* ui, SDL_Renderer* renderer, const NomosLayout* layout,
	NomosDocument* doc, const EntityDefs* entity_defs, const NomosTextureList* textures,
	NomosDialogState* dialog) {
	
	if (!ui || !renderer || !layout) return;
	
	// Reset click state at start of frame
	NomosMenuAction menu_action = render_menu_bar(ui, renderer, layout->menu_bar);
	render_left_panel(ui, renderer, layout->left_panel, doc, entity_defs);
	render_right_panel(ui, renderer, layout->right_panel, doc, textures, dialog);
	render_status_bar(renderer, layout->status_bar, doc);
	
	// Render dropdown menus LAST so they appear on top of panels
	NomosMenuAction dropdown_action = render_menu_dropdowns(ui, renderer, layout->menu_bar);
	if (dropdown_action != NOMOS_MENU_NONE) {
		menu_action = dropdown_action;
	}
	
	// Handle menu action
	if (menu_action != NOMOS_MENU_NONE) {
		// Trigger the action through the dialog system or directly
		extern void nomos_do_menu_action(NomosMenuAction action);
		nomos_do_menu_action(menu_action);
	}
	
	// Reset click at end of frame
	ui->mouse_clicked = false;
}

// Dialog functions
void nomos_dialog_init(NomosDialogState* dialog) {
	if (!dialog) return;
	memset(dialog, 0, sizeof(*dialog));
	dialog->gen_params.seed = 12345;
	dialog->gen_params.room_count = 8;
	dialog->gen_params.map_width = 64;
	dialog->gen_params.map_height = 64;
	dialog->gen_params.corridor_density = 0.5f;
	dialog->gen_params.outdoor_pockets = 0;
}

void nomos_dialog_destroy(NomosDialogState* dialog) {
	if (!dialog) return;
	memset(dialog, 0, sizeof(*dialog));
}

void nomos_dialog_show_open(NomosDialogState* dialog, const AssetPaths* paths) {
	(void)paths;
	if (!dialog) return;
	dialog->type = NOMOS_DIALOG_OPEN;
	dialog->pending_action = NOMOS_MENU_OPEN;
	dialog->input_path[0] = '\0';
	dialog->input_cursor = 0;
	dialog->has_result = false;
}

void nomos_dialog_show_save_as(NomosDialogState* dialog, const AssetPaths* paths) {
	(void)paths;
	if (!dialog) return;
	dialog->type = NOMOS_DIALOG_SAVE_AS;
	dialog->pending_action = NOMOS_MENU_SAVE_AS;
	dialog->input_path[0] = '\0';
	dialog->input_cursor = 0;
	dialog->has_result = false;
}

void nomos_dialog_show_generate(NomosDialogState* dialog) {
	if (!dialog) return;
	dialog->type = NOMOS_DIALOG_GENERATE;
	dialog->pending_action = NOMOS_MENU_GENERATE;
	dialog->has_result = false;
}

void nomos_dialog_show_error(NomosDialogState* dialog, const char* message) {
	if (!dialog) return;
	dialog->type = NOMOS_DIALOG_ERROR;
	strncpy(dialog->error_message, message ? message : "Unknown error", sizeof(dialog->error_message) - 1);
	dialog->has_result = false;
}

void nomos_dialog_show_texture_picker(NomosDialogState* dialog) {
	if (!dialog) return;
	dialog->type = NOMOS_DIALOG_TEXTURE_PICKER;
	dialog->texture_scroll = 0;
	dialog->selected_texture = -1;
	dialog->texture_confirmed = false;
}

bool nomos_dialog_poll_result(NomosDialogState* dialog, NomosMenuAction* action, char* path_out, size_t path_size) {
	if (!dialog || !dialog->has_result) return false;
	
	if (action) *action = dialog->pending_action;
	if (path_out && path_size > 0) {
		strncpy(path_out, dialog->result_path, path_size - 1);
		path_out[path_size - 1] = '\0';
	}
	
	// Clear result
	dialog->has_result = false;
	dialog->type = NOMOS_DIALOG_NONE;
	
	return true;
}

void nomos_dialog_get_gen_params(NomosDialogState* dialog, NomosGenParams* params) {
	if (!dialog || !params) return;
	*params = dialog->gen_params;
}

bool nomos_dialog_handle_event(NomosDialogState* dialog, SDL_Event* event) {
	if (!dialog || dialog->type == NOMOS_DIALOG_NONE) return false;
	
	// Get UI scale for mouse coordinate scaling
	float scale = g_nomos_font.ui_scale;
	if (scale < 1.0f) scale = 1.0f;
	
	if (event->type == SDL_KEYDOWN) {
		// ESC to close dialog
		if (event->key.keysym.sym == SDLK_ESCAPE) {
			dialog->type = NOMOS_DIALOG_NONE;
			return true;
		}
		
		// Text input for file dialogs
		if (dialog->type == NOMOS_DIALOG_OPEN || dialog->type == NOMOS_DIALOG_SAVE_AS) {
			if (event->key.keysym.sym == SDLK_RETURN) {
				// Confirm
				strncpy(dialog->result_path, dialog->input_path, sizeof(dialog->result_path) - 1);
				dialog->has_result = true;
				return true;
			}
			if (event->key.keysym.sym == SDLK_BACKSPACE) {
				int len = (int)strlen(dialog->input_path);
				if (len > 0) {
					dialog->input_path[len - 1] = '\0';
				}
				return true;
			}
		}
	}
	
	if (event->type == SDL_TEXTINPUT) {
		if (dialog->type == NOMOS_DIALOG_OPEN || dialog->type == NOMOS_DIALOG_SAVE_AS) {
			size_t len = strlen(dialog->input_path);
			size_t add_len = strlen(event->text.text);
			if (len + add_len < sizeof(dialog->input_path) - 1) {
				strcat(dialog->input_path, event->text.text);
			}
			return true;
		}
	}
	
	// Handle mouse clicks for generate dialog
	if (dialog->type == NOMOS_DIALOG_GENERATE && event->type == SDL_MOUSEBUTTONDOWN) {
		int mx = (int)(event->button.x * scale);
		int my = (int)(event->button.y * scale);
		
		// Get dialog geometry (must match render)
		int render_w, render_h;
		SDL_GetRendererOutputSize(SDL_GetRenderer(SDL_GetWindowFromID(event->button.windowID)), &render_w, &render_h);
		
		int dialog_w = 400;
		int dialog_h = 300;
		int box_x = (render_w - dialog_w) / 2;
		int box_y = (render_h - dialog_h) / 2;
		int x = box_x + 16;
		int y = box_y + 16 + 32;
		
		int row_h = 28;
		int label_w = 120;
		int slider_w = dialog_w - 32 - label_w - 60;
		
		// Seed increment/decrement buttons
		{
			SDL_Rect dec_btn = {x + label_w + slider_w + 4, box_y + 16 + 32, 24, 20};
			SDL_Rect inc_btn = {x + label_w + slider_w + 32, box_y + 16 + 32, 24, 20};
			
			if (point_in_rect(mx, my, dec_btn)) {
				if (dialog->gen_params.seed > 0) dialog->gen_params.seed--;
				return true;
			}
			if (point_in_rect(mx, my, inc_btn)) {
				dialog->gen_params.seed++;
				return true;
			}
		}
		
		// Room count slider
		y = box_y + 16 + 32 + row_h;
		{
			SDL_Rect slider_rect = {x + label_w, y, slider_w, 20};
			if (point_in_rect(mx, my, slider_rect)) {
				float t = (float)(mx - slider_rect.x) / (float)slider_w;
				t = NOMOS_CLAMP(t, 0.0f, 1.0f);
				dialog->gen_params.room_count = 3 + (int)(t * (20 - 3));
				return true;
			}
		}
		
		// Map width slider
		y += row_h;
		{
			SDL_Rect slider_rect = {x + label_w, y, slider_w, 20};
			if (point_in_rect(mx, my, slider_rect)) {
				float t = (float)(mx - slider_rect.x) / (float)slider_w;
				t = NOMOS_CLAMP(t, 0.0f, 1.0f);
				dialog->gen_params.map_width = 32 + (int)(t * (256 - 32));
				return true;
			}
		}
		
		// Map height slider
		y += row_h;
		{
			SDL_Rect slider_rect = {x + label_w, y, slider_w, 20};
			if (point_in_rect(mx, my, slider_rect)) {
				float t = (float)(mx - slider_rect.x) / (float)slider_w;
				t = NOMOS_CLAMP(t, 0.0f, 1.0f);
				dialog->gen_params.map_height = 32 + (int)(t * (256 - 32));
				return true;
			}
		}
		
		// Generate button
		y += row_h + 16;
		{
			SDL_Rect btn = {x, y, 120, 32};
			if (point_in_rect(mx, my, btn)) {
				dialog->has_result = true;
				dialog->result_path[0] = '\0';
				return true;
			}
		}
		
		// Random seed button
		{
			SDL_Rect btn = {x + 140, y, 120, 32};
			if (point_in_rect(mx, my, btn)) {
				dialog->gen_params.seed = (uint32_t)SDL_GetTicks() ^ (uint32_t)rand();
				return true;
			}
		}
	}
	
	return false;
}

void nomos_dialog_render(NomosDialogState* dialog, SDL_Renderer* renderer, int window_width, int window_height) {
	if (!dialog || dialog->type == NOMOS_DIALOG_NONE) return;
	
	// Dim background
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_Rect full = {0, 0, window_width, window_height};
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 128);
	SDL_RenderFillRect(renderer, &full);
	
	// Dialog box
	int dialog_w = 400;
	int dialog_h = 200;
	
	if (dialog->type == NOMOS_DIALOG_GENERATE) {
		dialog_h = 300;
	}
	
	SDL_Rect box = {
		(window_width - dialog_w) / 2,
		(window_height - dialog_h) / 2,
		dialog_w,
		dialog_h
	};
	
	draw_rect(renderer, box, NOMOS_COLOR_BG_PANEL);
	draw_rect_outline(renderer, box, NOMOS_COLOR_BORDER);
	
	int y = box.y + 16;
	int x = box.x + 16;
	
	switch (dialog->type) {
		case NOMOS_DIALOG_OPEN:
			draw_text(renderer, x, y, "Open Map", NOMOS_COLOR_TEXT);
			y += 24;
			draw_text(renderer, x, y, "Enter filename (relative to Levels/):", NOMOS_COLOR_TEXT_DIM);
			y += 20;
			{
				SDL_Rect input = {x, y, dialog_w - 32, 24};
				draw_rect(renderer, input, 30, 30, 35, 255);
				draw_rect_outline(renderer, input, NOMOS_COLOR_BORDER);
				draw_text(renderer, x + 4, y + 6, dialog->input_path, NOMOS_COLOR_TEXT);
				// Cursor
				int tw = text_width(dialog->input_path);
				draw_rect(renderer, (SDL_Rect){x + 4 + tw, y + 4, 2, 16}, NOMOS_COLOR_ACCENT);
			}
			y += 40;
			draw_text(renderer, x, y, "Press ENTER to confirm, ESC to cancel", NOMOS_COLOR_TEXT_DIM);
			break;
			
		case NOMOS_DIALOG_SAVE_AS:
			draw_text(renderer, x, y, "Save Map As", NOMOS_COLOR_TEXT);
			y += 24;
			draw_text(renderer, x, y, "Enter filename (relative to Levels/):", NOMOS_COLOR_TEXT_DIM);
			y += 20;
			{
				SDL_Rect input = {x, y, dialog_w - 32, 24};
				draw_rect(renderer, input, 30, 30, 35, 255);
				draw_rect_outline(renderer, input, NOMOS_COLOR_BORDER);
				draw_text(renderer, x + 4, y + 6, dialog->input_path, NOMOS_COLOR_TEXT);
				int tw = text_width(dialog->input_path);
				draw_rect(renderer, (SDL_Rect){x + 4 + tw, y + 4, 2, 16}, NOMOS_COLOR_ACCENT);
			}
			y += 40;
			draw_text(renderer, x, y, "Press ENTER to confirm, ESC to cancel", NOMOS_COLOR_TEXT_DIM);
			break;
			
		case NOMOS_DIALOG_GENERATE: {
			// Get UI scale for mouse coordinate scaling
			float scale = g_nomos_font.ui_scale;
			if (scale < 1.0f) scale = 1.0f;
			
			// Get mouse position and scale for HiDPI
			int raw_mx, raw_my;
			Uint32 mb = SDL_GetMouseState(&raw_mx, &raw_my);
			int mx = (int)(raw_mx * scale);
			int my = (int)(raw_my * scale);
			(void)mb;
			
			draw_text(renderer, x, y, "Generate Map", NOMOS_COLOR_TEXT);
			y += 32;
			
			int row_h = 28;
			int label_w = 120;
			int slider_w = dialog_w - 32 - label_w - 60;
			char buf[64];
			
			// Seed
			draw_text(renderer, x, y + 4, "Seed:", NOMOS_COLOR_TEXT_DIM);
			{
				SDL_Rect slider_rect = {x + label_w, y, slider_w, 20};
				draw_rect(renderer, slider_rect, 35, 38, 42, 255);
				
				// Clickable increment/decrement
				SDL_Rect dec_btn = {x + label_w + slider_w + 4, y, 24, 20};
				SDL_Rect inc_btn = {x + label_w + slider_w + 32, y, 24, 20};
				
				bool dec_hov = point_in_rect(mx, my, dec_btn);
				bool inc_hov = point_in_rect(mx, my, inc_btn);
				
				draw_rect(renderer, dec_btn, dec_hov ? 70 : 50, dec_hov ? 70 : 50, dec_hov ? 75 : 55, 255);
				draw_rect(renderer, inc_btn, inc_hov ? 70 : 50, inc_hov ? 70 : 50, inc_hov ? 75 : 55, 255);
				draw_text(renderer, dec_btn.x + 8, dec_btn.y + 3, "-", NOMOS_COLOR_TEXT);
				draw_text(renderer, inc_btn.x + 8, inc_btn.y + 3, "+", NOMOS_COLOR_TEXT);
				
				snprintf(buf, sizeof(buf), "%u", dialog->gen_params.seed);
				draw_text(renderer, x + label_w + 4, y + 4, buf, NOMOS_COLOR_TEXT);
			}
			y += row_h;
			
			// Room Count
			draw_text(renderer, x, y + 4, "Room Count:", NOMOS_COLOR_TEXT_DIM);
			{
				SDL_Rect slider_rect = {x + label_w, y, slider_w, 20};
				draw_rect(renderer, slider_rect, 35, 38, 42, 255);
				
				// Thumb position
				float t = (float)(dialog->gen_params.room_count - 3) / (float)(20 - 3);
				int thumb_x = slider_rect.x + (int)(t * (float)(slider_w - 12));
				SDL_Rect thumb = {thumb_x, y + 2, 12, 16};
				draw_rect(renderer, thumb, NOMOS_COLOR_ACCENT);
				
				snprintf(buf, sizeof(buf), "%d", dialog->gen_params.room_count);
				draw_text(renderer, x + label_w + slider_w + 8, y + 4, buf, NOMOS_COLOR_TEXT);
			}
			y += row_h;
			
			// Map Width
			draw_text(renderer, x, y + 4, "Map Width:", NOMOS_COLOR_TEXT_DIM);
			{
				SDL_Rect slider_rect = {x + label_w, y, slider_w, 20};
				draw_rect(renderer, slider_rect, 35, 38, 42, 255);
				
				float t = (float)(dialog->gen_params.map_width - 32) / (float)(256 - 32);
				int thumb_x = slider_rect.x + (int)(t * (float)(slider_w - 12));
				SDL_Rect thumb = {thumb_x, y + 2, 12, 16};
				draw_rect(renderer, thumb, NOMOS_COLOR_ACCENT);
				
				snprintf(buf, sizeof(buf), "%d", dialog->gen_params.map_width);
				draw_text(renderer, x + label_w + slider_w + 8, y + 4, buf, NOMOS_COLOR_TEXT);
			}
			y += row_h;
			
			// Map Height
			draw_text(renderer, x, y + 4, "Map Height:", NOMOS_COLOR_TEXT_DIM);
			{
				SDL_Rect slider_rect = {x + label_w, y, slider_w, 20};
				draw_rect(renderer, slider_rect, 35, 38, 42, 255);
				
				float t = (float)(dialog->gen_params.map_height - 32) / (float)(256 - 32);
				int thumb_x = slider_rect.x + (int)(t * (float)(slider_w - 12));
				SDL_Rect thumb = {thumb_x, y + 2, 12, 16};
				draw_rect(renderer, thumb, NOMOS_COLOR_ACCENT);
				
				snprintf(buf, sizeof(buf), "%d", dialog->gen_params.map_height);
				draw_text(renderer, x + label_w + slider_w + 8, y + 4, buf, NOMOS_COLOR_TEXT);
			}
			y += row_h + 16;
			
			// Generate button
			{
				SDL_Rect btn = {x, y, 120, 32};
				bool hovered = point_in_rect(mx, my, btn);
				
				if (hovered) {
					draw_rect(renderer, btn, NOMOS_COLOR_BG_BUTTON_HOV);
				} else {
					draw_rect(renderer, btn, NOMOS_COLOR_BG_BUTTON_ACT);
				}
				draw_rect_outline(renderer, btn, NOMOS_COLOR_BORDER);
				draw_text(renderer, x + 24, y + 10, "Generate", NOMOS_COLOR_TEXT);
			}
			
			// Random seed button
			{
				SDL_Rect btn = {x + 140, y, 120, 32};
				bool hovered = point_in_rect(mx, my, btn);
				
				if (hovered) {
					draw_rect(renderer, btn, NOMOS_COLOR_BG_BUTTON_HOV);
				} else {
					draw_rect(renderer, btn, NOMOS_COLOR_BG_BUTTON);
				}
				draw_rect_outline(renderer, btn, NOMOS_COLOR_BORDER);
				draw_text(renderer, x + 140 + 12, y + 10, "Random Seed", NOMOS_COLOR_TEXT);
			}
			
			y += 48;
			draw_text(renderer, x, y, "Press ESC to cancel", NOMOS_COLOR_TEXT_DIM);
			break;
		}
			
		case NOMOS_DIALOG_ERROR:
			draw_text(renderer, x, y, "Error", NOMOS_COLOR_ERROR);
			y += 28;
			draw_text(renderer, x, y, dialog->error_message, NOMOS_COLOR_TEXT);
			y += 40;
			draw_text(renderer, x, y, "Press ESC to close", NOMOS_COLOR_TEXT_DIM);
			break;
			
		default:
			break;
	}
	
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

// Widget implementations (simplified)
NomosWidgetResult nomos_ui_button(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label) {
	NomosWidgetResult result = {false, false, false};
	if (!ui || !renderer) return result;
	
	result.hovered = point_in_rect(ui->mouse_x, ui->mouse_y, rect);
	result.clicked = result.hovered && ui->mouse_clicked;
	
	if (result.hovered) {
		draw_rect(renderer, rect, NOMOS_COLOR_BG_BUTTON_HOV);
	} else {
		draw_rect(renderer, rect, NOMOS_COLOR_BG_BUTTON);
	}
	draw_rect_outline(renderer, rect, NOMOS_COLOR_BORDER);
	
	if (label) {
		int tw = text_width(label);
		int tx = rect.x + (rect.w - tw) / 2;
		int ty = rect.y + (rect.h - 12) / 2;
		draw_text(renderer, tx, ty, label, NOMOS_COLOR_TEXT);
	}
	
	return result;
}

NomosWidgetResult nomos_ui_label(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* text, bool dim) {
	NomosWidgetResult result = {false, false, false};
	(void)ui;
	
	if (text) {
		if (dim) {
			draw_text(renderer, rect.x, rect.y, text, NOMOS_COLOR_TEXT_DIM);
		} else {
			draw_text(renderer, rect.x, rect.y, text, NOMOS_COLOR_TEXT);
		}
	}
	
	return result;
}

NomosWidgetResult nomos_ui_checkbox(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label, bool* value) {
	NomosWidgetResult result = {false, false, false};
	if (!ui || !renderer || !value) return result;
	
	SDL_Rect box = {rect.x, rect.y + 2, 16, 16};
	result.hovered = point_in_rect(ui->mouse_x, ui->mouse_y, box);
	result.clicked = result.hovered && ui->mouse_clicked;
	
	if (result.clicked) {
		*value = !(*value);
		result.value_changed = true;
	}
	
	draw_rect_outline(renderer, box, NOMOS_COLOR_BORDER);
	if (*value) {
		SDL_Rect check = {box.x + 3, box.y + 3, 10, 10};
		draw_rect(renderer, check, NOMOS_COLOR_ACCENT);
	}
	
	if (label) {
		draw_text(renderer, rect.x + 22, rect.y + 4, label, NOMOS_COLOR_TEXT);
	}
	
	return result;
}

NomosWidgetResult nomos_ui_slider_int(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label, int* value, int min_val, int max_val) {
	NomosWidgetResult result = {false, false, false};
	if (!ui || !renderer || !value) return result;
	
	// Label
	if (label) {
		draw_text(renderer, rect.x, rect.y, label, NOMOS_COLOR_TEXT_DIM);
	}
	
	// Slider track
	int track_y = rect.y + 16;
	SDL_Rect track = {rect.x, track_y, rect.w, 8};
	draw_rect(renderer, track, 30, 30, 35, 255);
	
	// Slider thumb position
	float t = (float)(*value - min_val) / (float)(max_val - min_val);
	int thumb_x = rect.x + (int)(t * (float)(rect.w - 12));
	SDL_Rect thumb = {thumb_x, track_y - 2, 12, 12};
	
	result.hovered = point_in_rect(ui->mouse_x, ui->mouse_y, track) ||
	                 point_in_rect(ui->mouse_x, ui->mouse_y, thumb);
	
	if (result.hovered && ui->mouse_down) {
		float new_t = (float)(ui->mouse_x - rect.x) / (float)rect.w;
		new_t = NOMOS_CLAMP(new_t, 0.0f, 1.0f);
		int new_val = min_val + (int)(new_t * (float)(max_val - min_val));
		if (new_val != *value) {
			*value = new_val;
			result.value_changed = true;
		}
	}
	
	draw_rect(renderer, thumb, NOMOS_COLOR_ACCENT);
	
	// Value display
	char buf[32];
	snprintf(buf, sizeof(buf), "%d", *value);
	draw_text(renderer, rect.x + rect.w - text_width(buf), rect.y, buf, NOMOS_COLOR_TEXT);
	
	return result;
}

NomosWidgetResult nomos_ui_slider_float(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label, float* value, float min_val, float max_val) {
	NomosWidgetResult result = {false, false, false};
	if (!ui || !renderer || !value) return result;
	
	// Label
	if (label) {
		draw_text(renderer, rect.x, rect.y, label, NOMOS_COLOR_TEXT_DIM);
	}
	
	// Slider track
	int track_y = rect.y + 16;
	SDL_Rect track = {rect.x, track_y, rect.w, 8};
	draw_rect(renderer, track, 30, 30, 35, 255);
	
	// Slider thumb position
	float t = (*value - min_val) / (max_val - min_val);
	int thumb_x = rect.x + (int)(t * (float)(rect.w - 12));
	SDL_Rect thumb = {thumb_x, track_y - 2, 12, 12};
	
	result.hovered = point_in_rect(ui->mouse_x, ui->mouse_y, track) ||
	                 point_in_rect(ui->mouse_x, ui->mouse_y, thumb);
	
	if (result.hovered && ui->mouse_down) {
		float new_t = (float)(ui->mouse_x - rect.x) / (float)rect.w;
		new_t = NOMOS_CLAMP(new_t, 0.0f, 1.0f);
		float new_val = min_val + new_t * (max_val - min_val);
		if (new_val != *value) {
			*value = new_val;
			result.value_changed = true;
		}
	}
	
	draw_rect(renderer, thumb, NOMOS_COLOR_ACCENT);
	
	// Value display
	char buf[32];
	snprintf(buf, sizeof(buf), "%.2f", *value);
	draw_text(renderer, rect.x + rect.w - text_width(buf), rect.y, buf, NOMOS_COLOR_TEXT);
	
	return result;
}

NomosWidgetResult nomos_ui_text_input(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, char* buffer, size_t buffer_size, int* cursor) {
	NomosWidgetResult result = {false, false, false};
	(void)ui;
	(void)cursor;
	(void)buffer_size;
	
	draw_rect(renderer, rect, 30, 30, 35, 255);
	draw_rect_outline(renderer, rect, NOMOS_COLOR_BORDER);
	
	if (buffer) {
		draw_text(renderer, rect.x + 4, rect.y + 6, buffer, NOMOS_COLOR_TEXT);
	}
	
	return result;
}
