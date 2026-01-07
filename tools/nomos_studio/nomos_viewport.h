// Nomos Studio - Viewport rendering and interaction
#pragma once

#include "nomos.h"
#include "nomos_document.h"
#include "nomos_ui.h"
#include "game/entities.h"

#include <SDL.h>

// Viewport state for pan/zoom and interaction
typedef struct NomosViewport {
	// View transform
	float pan_x;      // World X at center of viewport
	float pan_y;      // World Y at center of viewport
	float zoom;       // Pixels per world unit
	
	// Interaction state
	bool panning;
	int pan_start_mouse_x;
	int pan_start_mouse_y;
	float pan_start_world_x;
	float pan_start_world_y;
	
	// Dragging state
	bool dragging;
	NomosSelectionType drag_type;
	int drag_index;
	float drag_offset_x;
	float drag_offset_y;
	
} NomosViewport;

// Lifecycle
void nomos_viewport_init(NomosViewport* vp);
void nomos_viewport_destroy(NomosViewport* vp);

// Event handling
void nomos_viewport_handle_event(NomosViewport* vp, SDL_Event* event, NomosDocument* doc,
	NomosUI* ui, const EntityDefs* entity_defs, int window_width, int window_height);

// Rendering
void nomos_viewport_render(NomosViewport* vp, SDL_Renderer* renderer, const SDL_Rect* rect,
	NomosDocument* doc);

// Fit view to map bounds
void nomos_viewport_fit_to_map(NomosViewport* vp, const NomosDocument* doc);

// Coordinate transforms
void nomos_viewport_world_to_screen(const NomosViewport* vp, const SDL_Rect* rect,
	float wx, float wy, int* sx, int* sy);
void nomos_viewport_screen_to_world(const NomosViewport* vp, const SDL_Rect* rect,
	int sx, int sy, float* wx, float* wy);
