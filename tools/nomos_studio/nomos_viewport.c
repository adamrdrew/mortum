// Nomos Studio - Viewport implementation
#include "nomos_viewport.h"
#include "nomos.h"
#include "nomos_document.h"
#include "nomos_font.h"

#include "game/world.h"

#include <SDL.h>
#include <math.h>
#include <stdio.h>

// External font for getting UI scale
extern NomosFont g_nomos_font;

#define MIN_ZOOM 2.0f
#define MAX_ZOOM 200.0f
#define DEFAULT_ZOOM 20.0f

static void draw_circle(SDL_Renderer* r, int cx, int cy, int radius) {
	// Simple circle approximation using line segments
	const int segments = 32;
	for (int i = 0; i < segments; i++) {
		float a1 = (float)i / (float)segments * 2.0f * 3.14159f;
		float a2 = (float)(i + 1) / (float)segments * 2.0f * 3.14159f;
		int x1 = cx + (int)(cosf(a1) * (float)radius);
		int y1 = cy + (int)(sinf(a1) * (float)radius);
		int x2 = cx + (int)(cosf(a2) * (float)radius);
		int y2 = cy + (int)(sinf(a2) * (float)radius);
		SDL_RenderDrawLine(r, x1, y1, x2, y2);
	}
}

static void fill_circle(SDL_Renderer* r, int cx, int cy, int radius) {
	// Simple filled circle
	for (int dy = -radius; dy <= radius; dy++) {
		int dx = (int)sqrtf((float)(radius * radius - dy * dy));
		SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
	}
}

static float point_to_segment_dist_sq(float px, float py, float x1, float y1, float x2, float y2) {
	float dx = x2 - x1;
	float dy = y2 - y1;
	float len_sq = dx * dx + dy * dy;
	
	if (len_sq < 0.0001f) {
		// Degenerate segment
		float ddx = px - x1;
		float ddy = py - y1;
		return ddx * ddx + ddy * ddy;
	}
	
	float t = ((px - x1) * dx + (py - y1) * dy) / len_sq;
	t = NOMOS_CLAMP(t, 0.0f, 1.0f);
	
	float nearest_x = x1 + t * dx;
	float nearest_y = y1 + t * dy;
	
	float ddx = px - nearest_x;
	float ddy = py - nearest_y;
	return ddx * ddx + ddy * ddy;
}

void nomos_viewport_init(NomosViewport* vp) {
	if (!vp) return;
	memset(vp, 0, sizeof(*vp));
	vp->zoom = DEFAULT_ZOOM;
}

void nomos_viewport_destroy(NomosViewport* vp) {
	if (!vp) return;
	memset(vp, 0, sizeof(*vp));
}

void nomos_viewport_world_to_screen(const NomosViewport* vp, const SDL_Rect* rect,
	float wx, float wy, int* sx, int* sy) {
	if (!vp || !rect) return;
	
	int cx = rect->x + rect->w / 2;
	int cy = rect->y + rect->h / 2;
	
	if (sx) *sx = cx + (int)((wx - vp->pan_x) * vp->zoom);
	if (sy) *sy = cy - (int)((wy - vp->pan_y) * vp->zoom); // Y is flipped
}

void nomos_viewport_screen_to_world(const NomosViewport* vp, const SDL_Rect* rect,
	int sx, int sy, float* wx, float* wy) {
	if (!vp || !rect) return;
	
	int cx = rect->x + rect->w / 2;
	int cy = rect->y + rect->h / 2;
	
	if (wx) *wx = vp->pan_x + (float)(sx - cx) / vp->zoom;
	if (wy) *wy = vp->pan_y - (float)(sy - cy) / vp->zoom; // Y is flipped
}

void nomos_viewport_fit_to_map(NomosViewport* vp, const NomosDocument* doc) {
	if (!vp || !doc || !doc->has_map) return;
	
	float min_x, min_y, max_x, max_y;
	if (!nomos_document_get_world_bounds(doc, &min_x, &min_y, &max_x, &max_y)) {
		vp->pan_x = 0;
		vp->pan_y = 0;
		vp->zoom = DEFAULT_ZOOM;
		return;
	}
	
	// Center on map
	vp->pan_x = (min_x + max_x) / 2.0f;
	vp->pan_y = (min_y + max_y) / 2.0f;
	
	// Compute zoom to fit (assume a reasonable viewport size)
	float map_w = max_x - min_x;
	float map_h = max_y - min_y;
	if (map_w < 1.0f) map_w = 1.0f;
	if (map_h < 1.0f) map_h = 1.0f;
	
	// Assume 800x600 viewport for fitting; actual size will be handled at render time
	float zoom_w = 700.0f / map_w;
	float zoom_h = 500.0f / map_h;
	vp->zoom = NOMOS_MIN(zoom_w, zoom_h);
	vp->zoom = NOMOS_CLAMP(vp->zoom, MIN_ZOOM, MAX_ZOOM);
}

void nomos_viewport_handle_event(NomosViewport* vp, SDL_Event* event, NomosDocument* doc,
	NomosUI* ui, const EntityDefs* entity_defs, int window_width, int window_height) {
	if (!vp || !event) return;
	
	// Get UI scale for converting logical mouse coords to render coords
	float scale = g_nomos_font.ui_scale;
	if (scale < 1.0f) scale = 1.0f;
	
	// Calculate viewport rect (in render coordinates)
	NomosLayout layout;
	nomos_ui_calculate_layout(&layout, window_width, window_height);
	SDL_Rect rect = layout.viewport;
	
	// Get mouse position and scale to render coordinates
	int mx = 0, my = 0;
	if (event->type == SDL_MOUSEMOTION) {
		mx = (int)(event->motion.x * scale);
		my = (int)(event->motion.y * scale);
	} else if (event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEBUTTONUP) {
		mx = (int)(event->button.x * scale);
		my = (int)(event->button.y * scale);
	} else if (event->type == SDL_MOUSEWHEEL) {
		int raw_mx, raw_my;
		SDL_GetMouseState(&raw_mx, &raw_my);
		mx = (int)(raw_mx * scale);
		my = (int)(raw_my * scale);
	}
	
	bool in_viewport = (mx >= rect.x && mx < rect.x + rect.w && my >= rect.y && my < rect.y + rect.h);
	
	switch (event->type) {
		case SDL_MOUSEWHEEL: {
			if (!in_viewport) break;
			
			// Zoom in/out
			float old_zoom = vp->zoom;
			float zoom_factor = event->wheel.y > 0 ? 1.15f : (1.0f / 1.15f);
			vp->zoom *= zoom_factor;
			vp->zoom = NOMOS_CLAMP(vp->zoom, MIN_ZOOM, MAX_ZOOM);
			
			// Zoom toward mouse position
			float world_x, world_y;
			nomos_viewport_screen_to_world(vp, &rect, mx, my, &world_x, &world_y);
			
			// Adjust pan to keep the same world point under the mouse
			float ratio = vp->zoom / old_zoom;
			vp->pan_x = world_x + (vp->pan_x - world_x) / ratio;
			vp->pan_y = world_y + (vp->pan_y - world_y) / ratio;
			break;
		}
		
		case SDL_MOUSEBUTTONDOWN: {
			if (!in_viewport) break;
			
			if (event->button.button == SDL_BUTTON_MIDDLE ||
				(event->button.button == SDL_BUTTON_LEFT && (SDL_GetModState() & KMOD_SHIFT))) {
				// Start panning
				vp->panning = true;
				vp->pan_start_mouse_x = mx;
				vp->pan_start_mouse_y = my;
				vp->pan_start_world_x = vp->pan_x;
				vp->pan_start_world_y = vp->pan_y;
			} else if (event->button.button == SDL_BUTTON_LEFT && doc && doc->has_map) {
				// Convert to world coordinates
				float world_x, world_y;
				nomos_viewport_screen_to_world(vp, &rect, mx, my, &world_x, &world_y);
				
				// Hit testing
				// Priority: entities/lights/particles/player_start > walls > sectors
				
				bool found = false;
				
				// Check entities
				for (int i = 0; i < doc->map.entity_count && !found; i++) {
					MapEntityPlacement* e = &doc->map.entities[i];
					float dx = e->x - world_x;
					float dy = e->y - world_y;
					float dist_sq = dx * dx + dy * dy;
					float hit_radius = 0.5f;
					if (dist_sq < hit_radius * hit_radius) {
						nomos_document_select(doc, NOMOS_SEL_ENTITY, i);
						vp->dragging = true;
						vp->drag_type = NOMOS_SEL_ENTITY;
						vp->drag_index = i;
						vp->drag_offset_x = e->x - world_x;
						vp->drag_offset_y = e->y - world_y;
						found = true;
					}
				}
				
				// Check lights
				for (int i = 0; i < doc->map.world.light_count && !found; i++) {
					if (!doc->map.world.light_alive || !doc->map.world.light_alive[i]) continue;
					PointLight* l = &doc->map.world.lights[i];
					float dx = l->x - world_x;
					float dy = l->y - world_y;
					float dist_sq = dx * dx + dy * dy;
					float hit_radius = 0.3f;
					if (dist_sq < hit_radius * hit_radius) {
						nomos_document_select(doc, NOMOS_SEL_LIGHT, i);
						vp->dragging = true;
						vp->drag_type = NOMOS_SEL_LIGHT;
						vp->drag_index = i;
						vp->drag_offset_x = l->x - world_x;
						vp->drag_offset_y = l->y - world_y;
						found = true;
					}
				}
				
				// Check particles
				for (int i = 0; i < doc->map.particle_count && !found; i++) {
					MapParticleEmitter* p = &doc->map.particles[i];
					float dx = p->x - world_x;
					float dy = p->y - world_y;
					float dist_sq = dx * dx + dy * dy;
					float hit_radius = 0.3f;
					if (dist_sq < hit_radius * hit_radius) {
						nomos_document_select(doc, NOMOS_SEL_PARTICLE, i);
						vp->dragging = true;
						vp->drag_type = NOMOS_SEL_PARTICLE;
						vp->drag_index = i;
						vp->drag_offset_x = p->x - world_x;
						vp->drag_offset_y = p->y - world_y;
						found = true;
					}
				}
				
				// Check player start
				if (!found) {
					float dx = doc->map.player_start_x - world_x;
					float dy = doc->map.player_start_y - world_y;
					float dist_sq = dx * dx + dy * dy;
					float hit_radius = 0.4f;
					if (dist_sq < hit_radius * hit_radius) {
						nomos_document_select(doc, NOMOS_SEL_PLAYER_START, 0);
						vp->dragging = true;
						vp->drag_type = NOMOS_SEL_PLAYER_START;
						vp->drag_index = 0;
						vp->drag_offset_x = doc->map.player_start_x - world_x;
						vp->drag_offset_y = doc->map.player_start_y - world_y;
						found = true;
					}
				}
				
				// Check walls
				if (!found) {
					float best_dist_sq = 100.0f; // Threshold in world units squared
					int best_wall = -1;
					
					for (int i = 0; i < doc->map.world.wall_count; i++) {
						Wall* w = &doc->map.world.walls[i];
						Vertex v0 = doc->map.world.vertices[w->v0];
						Vertex v1 = doc->map.world.vertices[w->v1];
						
						float dist_sq = point_to_segment_dist_sq(world_x, world_y, v0.x, v0.y, v1.x, v1.y);
						// Convert threshold to world units based on zoom
						float threshold = (8.0f / vp->zoom) * (8.0f / vp->zoom);
						if (dist_sq < threshold && dist_sq < best_dist_sq) {
							best_dist_sq = dist_sq;
							best_wall = i;
						}
					}
					
					if (best_wall >= 0) {
						nomos_document_select(doc, NOMOS_SEL_WALL, best_wall);
						found = true;
					}
				}
				
				// Check sectors (point in polygon)
				if (!found) {
					int sector = nomos_document_find_sector_at_point(doc, world_x, world_y);
					if (sector >= 0) {
						nomos_document_select(doc, NOMOS_SEL_SECTOR, sector);
						found = true;
					}
				}
				
				// If nothing found but in palette placement mode, place new object
				if (!found && ui) {
					int sector = nomos_document_find_sector_at_point(doc, world_x, world_y);
					if (sector >= 0) {
						switch (ui->palette_mode) {
							case NOMOS_PALETTE_ENTITIES:
								if (ui->palette_selected >= 0 && entity_defs && 
									(uint32_t)ui->palette_selected < entity_defs->count) {
									const char* def_name = entity_defs->defs[ui->palette_selected].name;
									int idx = nomos_document_add_entity(doc, def_name, world_x, world_y);
									if (idx >= 0) {
										nomos_document_select(doc, NOMOS_SEL_ENTITY, idx);
										printf("Placed entity '%s' at %.2f, %.2f\n", def_name, world_x, world_y);
									}
								}
								break;
							case NOMOS_PALETTE_LIGHTS: {
								int idx = nomos_document_add_light(doc, world_x, world_y, 1.0f, 4.0f, 1.0f);
								if (idx >= 0) {
									nomos_document_select(doc, NOMOS_SEL_LIGHT, idx);
								}
								break;
							}
							case NOMOS_PALETTE_PARTICLES: {
								int idx = nomos_document_add_particle(doc, world_x, world_y, 0.5f);
								if (idx >= 0) {
									nomos_document_select(doc, NOMOS_SEL_PARTICLE, idx);
								}
								break;
							}
							case NOMOS_PALETTE_PLAYER_START:
								nomos_document_move_player_start(doc, world_x, world_y);
								nomos_document_select(doc, NOMOS_SEL_PLAYER_START, 0);
								break;
						}
					}
				}
				
				if (!found) {
					nomos_document_deselect_all(doc);
				}
			}
			break;
		}
		
		case SDL_MOUSEBUTTONUP: {
			if (event->button.button == SDL_BUTTON_MIDDLE ||
				event->button.button == SDL_BUTTON_LEFT) {
				vp->panning = false;
				vp->dragging = false;
			}
			break;
		}
		
		case SDL_MOUSEMOTION: {
			if (vp->panning) {
				// Update pan
				float dx = (float)(mx - vp->pan_start_mouse_x) / vp->zoom;
				float dy = (float)(my - vp->pan_start_mouse_y) / vp->zoom;
				vp->pan_x = vp->pan_start_world_x - dx;
				vp->pan_y = vp->pan_start_world_y + dy; // Y is flipped
			} else if (vp->dragging && doc && doc->has_map) {
				// Drag the selected object
				float world_x, world_y;
				nomos_viewport_screen_to_world(vp, &rect, mx, my, &world_x, &world_y);
				world_x += vp->drag_offset_x;
				world_y += vp->drag_offset_y;
				
				switch (vp->drag_type) {
					case NOMOS_SEL_ENTITY:
						nomos_document_move_entity(doc, vp->drag_index, world_x, world_y);
						break;
					case NOMOS_SEL_LIGHT:
						nomos_document_move_light(doc, vp->drag_index, world_x, world_y);
						break;
					case NOMOS_SEL_PARTICLE:
						nomos_document_move_particle(doc, vp->drag_index, world_x, world_y);
						break;
					case NOMOS_SEL_PLAYER_START:
						nomos_document_move_player_start(doc, world_x, world_y);
						break;
					default:
						break;
				}
			}
			break;
		}
		
		default:
			break;
	}
}

void nomos_viewport_render(NomosViewport* vp, SDL_Renderer* renderer, const SDL_Rect* rect,
	NomosDocument* doc) {
	if (!vp || !renderer || !rect) return;
	
	// Background
	SDL_SetRenderDrawColor(renderer, 35, 38, 42, 255);
	SDL_RenderFillRect(renderer, rect);
	
	// Clip to viewport
	SDL_RenderSetClipRect(renderer, rect);
	
	// Draw grid
	{
		SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_GRID);
		
		// Calculate grid spacing (aim for ~50 pixel spacing)
		float grid_size = 1.0f;
		while (grid_size * vp->zoom < 30.0f) grid_size *= 2.0f;
		while (grid_size * vp->zoom > 100.0f) grid_size /= 2.0f;
		
		// Calculate visible bounds
		float min_x, min_y, max_x, max_y;
		nomos_viewport_screen_to_world(vp, rect, rect->x, rect->y + rect->h, &min_x, &min_y);
		nomos_viewport_screen_to_world(vp, rect, rect->x + rect->w, rect->y, &max_x, &max_y);
		
		// Draw vertical lines
		float start_x = floorf(min_x / grid_size) * grid_size;
		for (float x = start_x; x <= max_x; x += grid_size) {
			int sx1, sy1, sx2, sy2;
			nomos_viewport_world_to_screen(vp, rect, x, min_y, &sx1, &sy1);
			nomos_viewport_world_to_screen(vp, rect, x, max_y, &sx2, &sy2);
			SDL_RenderDrawLine(renderer, sx1, sy1, sx2, sy2);
		}
		
		// Draw horizontal lines
		float start_y = floorf(min_y / grid_size) * grid_size;
		for (float y = start_y; y <= max_y; y += grid_size) {
			int sx1, sy1, sx2, sy2;
			nomos_viewport_world_to_screen(vp, rect, min_x, y, &sx1, &sy1);
			nomos_viewport_world_to_screen(vp, rect, max_x, y, &sx2, &sy2);
			SDL_RenderDrawLine(renderer, sx1, sy1, sx2, sy2);
		}
		
		// Origin axes
		SDL_SetRenderDrawColor(renderer, 80, 80, 90, 255);
		int ox, oy;
		nomos_viewport_world_to_screen(vp, rect, 0, 0, &ox, &oy);
		SDL_RenderDrawLine(renderer, rect->x, oy, rect->x + rect->w, oy);
		SDL_RenderDrawLine(renderer, ox, rect->y, ox, rect->y + rect->h);
	}
	
	if (!doc || !doc->has_map) {
		SDL_RenderSetClipRect(renderer, NULL);
		return;
	}
	
	const World* world = &doc->map.world;
	
	// Draw sector fills (optional, for visual feedback)
	// Skip for now to keep rendering simple
	
	// Draw walls
	for (int i = 0; i < world->wall_count; i++) {
		Wall* w = &world->walls[i];
		Vertex v0 = world->vertices[w->v0];
		Vertex v1 = world->vertices[w->v1];
		
		int sx1, sy1, sx2, sy2;
		nomos_viewport_world_to_screen(vp, rect, v0.x, v0.y, &sx1, &sy1);
		nomos_viewport_world_to_screen(vp, rect, v1.x, v1.y, &sx2, &sy2);
		
		// Color based on wall type
		if (doc->selection.type == NOMOS_SEL_WALL && doc->selection.index == i) {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_SELECTED);
		} else if (w->door_blocked) {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_WALL_DOOR);
		} else if (w->back_sector >= 0) {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_WALL_PORTAL);
		} else {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_WALL_SOLID);
		}
		
		SDL_RenderDrawLine(renderer, sx1, sy1, sx2, sy2);
		
		// Draw wall normal indicator (small perpendicular tick)
		if (vp->zoom > 10.0f) {
			float mx = (v0.x + v1.x) / 2.0f;
			float my = (v0.y + v1.y) / 2.0f;
			float dx = v1.x - v0.x;
			float dy = v1.y - v0.y;
			float len = sqrtf(dx * dx + dy * dy);
			if (len > 0.01f) {
				float nx = -dy / len * 0.15f;
				float ny = dx / len * 0.15f;
				int nmx, nmy, nnx, nny;
				nomos_viewport_world_to_screen(vp, rect, mx, my, &nmx, &nmy);
				nomos_viewport_world_to_screen(vp, rect, mx + nx, my + ny, &nnx, &nny);
				SDL_RenderDrawLine(renderer, nmx, nmy, nnx, nny);
			}
		}
	}
	
	// Highlight selected sector
	if (doc->selection.type == NOMOS_SEL_SECTOR && doc->selection.index >= 0 &&
		doc->selection.index < world->sector_count) {
		// Draw the sector's walls in a highlight color
		int sector = doc->selection.index;
		for (int i = 0; i < world->wall_count; i++) {
			Wall* w = &world->walls[i];
			if (w->front_sector == sector || w->back_sector == sector) {
				Vertex v0 = world->vertices[w->v0];
				Vertex v1 = world->vertices[w->v1];
				
				int sx1, sy1, sx2, sy2;
				nomos_viewport_world_to_screen(vp, rect, v0.x, v0.y, &sx1, &sy1);
				nomos_viewport_world_to_screen(vp, rect, v1.x, v1.y, &sx2, &sy2);
				
				SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_SELECTED);
				SDL_RenderDrawLine(renderer, sx1, sy1, sx2, sy2);
			}
		}
	}
	
	// Draw entities
	for (int i = 0; i < doc->map.entity_count; i++) {
		MapEntityPlacement* e = &doc->map.entities[i];
		int sx, sy;
		nomos_viewport_world_to_screen(vp, rect, e->x, e->y, &sx, &sy);
		
		bool selected = (doc->selection.type == NOMOS_SEL_ENTITY && doc->selection.index == i);
		
		int size = 6;
		if (selected) {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_SELECTED);
			size = 8;
		} else {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_ENTITY);
		}
		
		SDL_Rect er = {sx - size, sy - size, size * 2, size * 2};
		SDL_RenderFillRect(renderer, &er);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
		SDL_RenderDrawRect(renderer, &er);
	}
	
	// Draw lights
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	for (int i = 0; i < world->light_count; i++) {
		if (!world->light_alive || !world->light_alive[i]) continue;
		PointLight* l = &world->lights[i];
		
		int sx, sy;
		nomos_viewport_world_to_screen(vp, rect, l->x, l->y, &sx, &sy);
		
		bool selected = (doc->selection.type == NOMOS_SEL_LIGHT && doc->selection.index == i);
		
		// Draw radius circle
		int radius_px = (int)(l->radius * vp->zoom);
		if (radius_px > 2) {
			Uint8 r = (Uint8)(l->color.r * 255);
			Uint8 g = (Uint8)(l->color.g * 255);
			Uint8 b = (Uint8)(l->color.b * 255);
			Uint8 a = (Uint8)(l->intensity * 50);
			if (a > 80) a = 80;
			SDL_SetRenderDrawColor(renderer, r, g, b, a);
			fill_circle(renderer, sx, sy, radius_px);
			
			SDL_SetRenderDrawColor(renderer, r, g, b, 150);
			draw_circle(renderer, sx, sy, radius_px);
		}
		
		// Draw center dot
		if (selected) {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_SELECTED);
		} else {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_LIGHT);
		}
		fill_circle(renderer, sx, sy, selected ? 6 : 4);
	}
	
	// Draw particle emitters
	for (int i = 0; i < doc->map.particle_count; i++) {
		MapParticleEmitter* p = &doc->map.particles[i];
		
		int sx, sy;
		nomos_viewport_world_to_screen(vp, rect, p->x, p->y, &sx, &sy);
		
		bool selected = (doc->selection.type == NOMOS_SEL_PARTICLE && doc->selection.index == i);
		
		if (selected) {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_SELECTED);
		} else {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_PARTICLE);
		}
		
		// Draw diamond shape
		int size = selected ? 8 : 6;
		SDL_Point points[5] = {
			{sx, sy - size},
			{sx + size, sy},
			{sx, sy + size},
			{sx - size, sy},
			{sx, sy - size}
		};
		SDL_RenderDrawLines(renderer, points, 5);
	}
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	
	// Draw player start
	{
		int sx, sy;
		nomos_viewport_world_to_screen(vp, rect, doc->map.player_start_x, doc->map.player_start_y, &sx, &sy);
		
		bool selected = (doc->selection.type == NOMOS_SEL_PLAYER_START);
		
		if (selected) {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_SELECTED);
		} else {
			SDL_SetRenderDrawColor(renderer, NOMOS_COLOR_PLAYER_START);
		}
		
		// Draw triangle pointing in direction
		float angle_rad = doc->map.player_start_angle_deg * 3.14159f / 180.0f;
		int size = selected ? 12 : 10;
		
		float dx = cosf(angle_rad) * (float)size;
		float dy = sinf(angle_rad) * (float)size;
		
		// Triangle points
		int px = sx + (int)dx;
		int py = sy - (int)dy;
		
		float perp_dx = -dy * 0.5f;
		float perp_dy = dx * 0.5f;
		
		int lx = sx - (int)(dx * 0.5f) + (int)perp_dx;
		int ly = sy + (int)(dy * 0.5f) - (int)perp_dy;
		int rx = sx - (int)(dx * 0.5f) - (int)perp_dx;
		int ry = sy + (int)(dy * 0.5f) + (int)perp_dy;
		
		SDL_RenderDrawLine(renderer, px, py, lx, ly);
		SDL_RenderDrawLine(renderer, lx, ly, rx, ry);
		SDL_RenderDrawLine(renderer, rx, ry, px, py);
		
		// Center dot
		fill_circle(renderer, sx, sy, 3);
	}
	
	SDL_RenderSetClipRect(renderer, NULL);
}
