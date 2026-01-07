// Nomos Studio - Map Editor for the Mortum Engine
// A standalone desktop application for authoring, editing, and testing Mortum maps.

#include "nomos.h"
#include "nomos_ui.h"
#include "nomos_document.h"
#include "nomos_viewport.h"
#include "nomos_procgen.h"
#include "nomos_save.h"

#include "assets/asset_paths.h"
#include "assets/map_loader.h"
#include "assets/map_validate.h"
#include "game/entities.h"
#include "core/log.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#define NOMOS_WINDOW_TITLE "Nomos Studio"
#define NOMOS_DEFAULT_WIDTH 1280
#define NOMOS_DEFAULT_HEIGHT 800
#define NOMOS_MIN_WIDTH 800
#define NOMOS_MIN_HEIGHT 600

// Application state
typedef struct NomosApp {
	SDL_Window* window;
	SDL_Renderer* renderer;
	bool running;
	bool request_quit;
	
	// Asset paths
	AssetPaths paths;
	
	// Entity definitions (for entity palette)
	EntityDefs entity_defs;
	
	// Current document
	NomosDocument doc;
	
	// UI state
	NomosUI ui;
	
	// Viewport state
	NomosViewport viewport;
	
	// Texture list for browsing
	NomosTextureList textures;
	
	// Pending dialogs
	NomosDialogState dialog;
	
	// Last frame time for delta time calculation
	uint32_t last_frame_ms;
	
	// Window dimensions (logical, in points)
	int window_width;
	int window_height;
	
	// HiDPI scale factor (render pixels / logical points)
	float ui_scale;
} NomosApp;

static NomosApp g_app;

// Font system (global for easy access from UI)
#include "nomos_font.h"
NomosFont g_nomos_font;

// Forward declarations
static bool nomos_init(void);
static void nomos_shutdown(void);
static void nomos_handle_event(SDL_Event* event);
static void nomos_update(float dt);
static void nomos_render(void);
void nomos_do_menu_action(NomosMenuAction action);

// Helper: find the Assets directory relative to the executable
static bool find_assets_root(char* out_path, size_t out_size) {
	// Try current directory first
	const char* candidates[] = {
		"Assets",
		"./Assets",
		"../Assets",
		"../../Assets"
	};
	
	for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
		char test[512];
		snprintf(test, sizeof(test), "%s/Entities/entities_manifest.json", candidates[i]);
		if (access(test, F_OK) == 0) {
			// Found it
			size_t len = strlen(candidates[i]);
			if (len < out_size) {
				// Get absolute path
				if (realpath(candidates[i], out_path) != NULL) {
					return true;
				}
				strcpy(out_path, candidates[i]);
				return true;
			}
		}
	}
	return false;
}

static bool nomos_init(void) {
	memset(&g_app, 0, sizeof(g_app));
	
	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
		return false;
	}
	
	// Enable high-DPI if available
	SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "0");
	
	// Create window
	g_app.window = SDL_CreateWindow(
		NOMOS_WINDOW_TITLE,
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		NOMOS_DEFAULT_WIDTH,
		NOMOS_DEFAULT_HEIGHT,
		SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
	);
	if (!g_app.window) {
		fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
		SDL_Quit();
		return false;
	}
	
	SDL_SetWindowMinimumSize(g_app.window, NOMOS_MIN_WIDTH, NOMOS_MIN_HEIGHT);
	
	// Create renderer
	g_app.renderer = SDL_CreateRenderer(g_app.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!g_app.renderer) {
		fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
		SDL_DestroyWindow(g_app.window);
		SDL_Quit();
		return false;
	}
	
	// Get initial window size and calculate DPI scale
	SDL_GetWindowSize(g_app.window, &g_app.window_width, &g_app.window_height);
	
	// Calculate HiDPI scale by comparing render size to window size
	int render_w, render_h;
	SDL_GetRendererOutputSize(g_app.renderer, &render_w, &render_h);
	g_app.ui_scale = (float)render_w / (float)g_app.window_width;
	if (g_app.ui_scale < 1.0f) g_app.ui_scale = 1.0f;
	printf("DPI scale: %.2f (window: %dx%d, render: %dx%d)\n", 
		g_app.ui_scale, g_app.window_width, g_app.window_height, render_w, render_h);
	
	// Find assets root
	char assets_root[512];
	if (!find_assets_root(assets_root, sizeof(assets_root))) {
		fprintf(stderr, "Could not find Assets directory\n");
		SDL_DestroyRenderer(g_app.renderer);
		SDL_DestroyWindow(g_app.window);
		SDL_Quit();
		return false;
	}
	
	// Initialize asset paths
	// Note: asset_paths_init expects the parent of Assets/, so we need to go up one level
	char base_path[512];
	snprintf(base_path, sizeof(base_path), "%s/..", assets_root);
	char* real_base = realpath(base_path, NULL);
	if (!real_base) {
		fprintf(stderr, "Could not resolve base path\n");
		SDL_DestroyRenderer(g_app.renderer);
		SDL_DestroyWindow(g_app.window);
		SDL_Quit();
		return false;
	}
	
	if (!asset_paths_init(&g_app.paths, real_base)) {
		fprintf(stderr, "asset_paths_init failed\n");
		free(real_base);
		SDL_DestroyRenderer(g_app.renderer);
		SDL_DestroyWindow(g_app.window);
		SDL_Quit();
		return false;
	}
	free(real_base);
	
	// Load entity definitions
	entity_defs_init(&g_app.entity_defs);
	if (!entity_defs_load(&g_app.entity_defs, &g_app.paths)) {
		fprintf(stderr, "Warning: Could not load entity definitions\n");
		// Not fatal - continue without entities
	}
	
	// Initialize font (using ProggyClean.ttf from Assets/Fonts)
	if (!nomos_font_init(&g_nomos_font, g_app.renderer, &g_app.paths, "ProggyClean.ttf", 13, g_app.ui_scale)) {
		fprintf(stderr, "Warning: Could not load font, text will not render\n");
	}
	
	// Initialize document (empty)
	nomos_document_init(&g_app.doc);
	
	// Initialize UI
	nomos_ui_init(&g_app.ui, g_app.renderer);
	
	// Initialize viewport
	nomos_viewport_init(&g_app.viewport);
	
	// Load texture list
	nomos_texture_list_load(&g_app.textures, &g_app.paths, g_app.renderer);
	
	// Initialize dialog state
	nomos_dialog_init(&g_app.dialog);
	
	g_app.running = true;
	g_app.last_frame_ms = SDL_GetTicks();
	
	printf("Nomos Studio initialized successfully\n");
	printf("Assets root: %s\n", g_app.paths.assets_root);
	printf("Entity definitions loaded: %d\n", g_app.entity_defs.count);
	
	return true;
}

static void nomos_shutdown(void) {
	nomos_dialog_destroy(&g_app.dialog);
	nomos_texture_list_destroy(&g_app.textures);
	nomos_viewport_destroy(&g_app.viewport);
	nomos_ui_destroy(&g_app.ui);
	nomos_document_destroy(&g_app.doc);
	nomos_font_destroy(&g_nomos_font);
	entity_defs_destroy(&g_app.entity_defs);
	asset_paths_destroy(&g_app.paths);
	
	if (g_app.renderer) {
		SDL_DestroyRenderer(g_app.renderer);
		g_app.renderer = NULL;
	}
	if (g_app.window) {
		SDL_DestroyWindow(g_app.window);
		g_app.window = NULL;
	}
	
	SDL_Quit();
	printf("Nomos Studio shutdown complete\n");
}

static void nomos_handle_event(SDL_Event* event) {
	// Let UI handle events first (for menus, dialogs, etc.)
	if (nomos_ui_handle_event(&g_app.ui, event, &g_app.doc, &g_app.dialog)) {
		return; // UI consumed the event
	}
	
	switch (event->type) {
		case SDL_QUIT:
			g_app.request_quit = true;
			break;
			
		case SDL_WINDOWEVENT:
			if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
				g_app.window_width = event->window.data1;
				g_app.window_height = event->window.data2;
			}
			break;
			
		case SDL_KEYDOWN: {
			SDL_Keymod mod = SDL_GetModState();
			bool ctrl = (mod & KMOD_CTRL) != 0;
			bool shift = (mod & KMOD_SHIFT) != 0;
			
			// Keyboard shortcuts
			switch (event->key.keysym.sym) {
				case SDLK_o:
					if (ctrl) nomos_do_menu_action(NOMOS_MENU_OPEN);
					break;
				case SDLK_s:
					if (ctrl && shift) nomos_do_menu_action(NOMOS_MENU_SAVE_AS);
					else if (ctrl) nomos_do_menu_action(NOMOS_MENU_SAVE);
					break;
				case SDLK_v:
					if (ctrl) nomos_do_menu_action(NOMOS_MENU_VALIDATE);
					break;
				case SDLK_g:
					if (ctrl) nomos_do_menu_action(NOMOS_MENU_GENERATE);
					break;
				case SDLK_r:
					if (ctrl) nomos_do_menu_action(NOMOS_MENU_RUN);
					break;
				case SDLK_DELETE:
				case SDLK_BACKSPACE:
					// Delete selected object
					nomos_document_delete_selected(&g_app.doc);
					break;
				case SDLK_ESCAPE:
					nomos_document_deselect_all(&g_app.doc);
					break;
				default:
					break;
			}
			break;
		}
		
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
		case SDL_MOUSEMOTION:
		case SDL_MOUSEWHEEL: {
			// Get render dimensions for proper HiDPI handling
			int render_w, render_h;
			SDL_GetRendererOutputSize(g_app.renderer, &render_w, &render_h);
			// Forward to viewport for pan/zoom/selection
			nomos_viewport_handle_event(&g_app.viewport, event, &g_app.doc, &g_app.ui,
				&g_app.entity_defs, render_w, render_h);
			break;
		}
			
		default:
			break;
	}
}

static void nomos_update(float dt) {
	(void)dt;
	
	// Handle dialog results
	NomosMenuAction completed_action;
	char result_path[512];
	if (nomos_dialog_poll_result(&g_app.dialog, &completed_action, result_path, sizeof(result_path))) {
		switch (completed_action) {
			case NOMOS_MENU_OPEN:
				if (result_path[0] != '\0') {
					if (nomos_document_load(&g_app.doc, &g_app.paths, result_path)) {
						printf("Loaded map: %s\n", result_path);
						nomos_viewport_fit_to_map(&g_app.viewport, &g_app.doc);
					} else {
						nomos_dialog_show_error(&g_app.dialog, "Failed to load map");
					}
				}
				break;
			case NOMOS_MENU_SAVE_AS:
				if (result_path[0] != '\0') {
					strncpy(g_app.doc.file_path, result_path, sizeof(g_app.doc.file_path) - 1);
					g_app.doc.file_path[sizeof(g_app.doc.file_path) - 1] = '\0';
					if (nomos_document_save(&g_app.doc, &g_app.paths)) {
						printf("Saved map: %s\n", result_path);
					} else {
						nomos_dialog_show_error(&g_app.dialog, "Failed to save map");
					}
				}
				break;
			case NOMOS_MENU_GENERATE: {
				// Generate with the parameters from dialog
				NomosGenParams ui_params;
				nomos_dialog_get_gen_params(&g_app.dialog, &ui_params);
				
				// Convert UI params to procgen params
				NomosProcGenParams params;
				nomos_procgen_params_default(&params);
				params.seed = ui_params.seed;
				params.target_room_count = ui_params.room_count;
				params.max_x = (float)ui_params.map_width;
				params.max_y = (float)ui_params.map_height;
				
				// Clear old map and generate new one
				nomos_document_clear(&g_app.doc);
				if (nomos_procgen_generate(&g_app.doc.map, &params)) {
					g_app.doc.has_map = true;
					g_app.doc.dirty = true;
					strncpy(g_app.doc.file_path, "untitled.json", sizeof(g_app.doc.file_path) - 1);
					printf("Generated map with seed %u\n", params.seed);
					nomos_viewport_fit_to_map(&g_app.viewport, &g_app.doc);
				} else {
					nomos_dialog_show_error(&g_app.dialog, "Failed to generate valid map");
				}
				break;
			}
			default:
				break;
		}
	}
	
	// Handle quit request
	if (g_app.request_quit) {
		if (g_app.doc.dirty) {
			// TODO: Prompt to save unsaved changes
			// For now, just quit
		}
		g_app.running = false;
	}
}

static void nomos_render(void) {
	// Clear background
	SDL_SetRenderDrawColor(g_app.renderer, 40, 40, 45, 255);
	SDL_RenderClear(g_app.renderer);
	
	// Get actual render dimensions (physical pixels for HiDPI)
	int render_w, render_h;
	SDL_GetRendererOutputSize(g_app.renderer, &render_w, &render_h);
	
	// Calculate layout regions
	NomosLayout layout;
	nomos_ui_calculate_layout(&layout, render_w, render_h);
	
	// Render viewport (main map view)
	nomos_viewport_render(&g_app.viewport, g_app.renderer, &layout.viewport, &g_app.doc);
	
	// Render UI panels
	nomos_ui_render(&g_app.ui, g_app.renderer, &layout, &g_app.doc, &g_app.entity_defs, 
		&g_app.textures, &g_app.dialog);
	
	// Render active dialog if any
	nomos_dialog_render(&g_app.dialog, g_app.renderer, render_w, render_h);
	
	SDL_RenderPresent(g_app.renderer);
}

void nomos_do_menu_action(NomosMenuAction action) {
	switch (action) {
		case NOMOS_MENU_OPEN:
			nomos_dialog_show_open(&g_app.dialog, &g_app.paths);
			break;
		case NOMOS_MENU_SAVE:
			if (g_app.doc.file_path[0] == '\0') {
				nomos_dialog_show_save_as(&g_app.dialog, &g_app.paths);
			} else {
				if (nomos_document_save(&g_app.doc, &g_app.paths)) {
					printf("Saved: %s\n", g_app.doc.file_path);
				} else {
					nomos_dialog_show_error(&g_app.dialog, "Failed to save map");
				}
			}
			break;
		case NOMOS_MENU_SAVE_AS:
			nomos_dialog_show_save_as(&g_app.dialog, &g_app.paths);
			break;
		case NOMOS_MENU_VALIDATE:
			nomos_document_validate(&g_app.doc, &g_app.paths);
			break;
		case NOMOS_MENU_EXIT:
			g_app.request_quit = true;
			break;
		case NOMOS_MENU_GENERATE:
			nomos_dialog_show_generate(&g_app.dialog);
			break;
		case NOMOS_MENU_RUN:
			nomos_document_run_in_mortum(&g_app.doc, &g_app.paths);
			break;
		default:
			break;
	}
}

int main(int argc, char* argv[]) {
	(void)argc;
	(void)argv;
	
	if (!nomos_init()) {
		return 1;
	}
	
	// Main loop
	while (g_app.running) {
		uint32_t now_ms = SDL_GetTicks();
		float dt = (float)(now_ms - g_app.last_frame_ms) / 1000.0f;
		g_app.last_frame_ms = now_ms;
		
		// Clamp delta time to avoid huge jumps
		if (dt > 0.1f) dt = 0.1f;
		
		// Process events
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			nomos_handle_event(&event);
		}
		
		// Update
		nomos_update(dt);
		
		// Render
		nomos_render();
	}
	
	nomos_shutdown();
	return 0;
}

// Texture list implementation (stubs for now)
void nomos_texture_list_load(NomosTextureList* list, const AssetPaths* paths __attribute__((unused)), SDL_Renderer* renderer __attribute__((unused))) {
	if (!list) return;
	memset(list, 0, sizeof(*list));
	// TODO: Scan Assets/Images/Textures and load thumbnails
}

void nomos_texture_list_destroy(NomosTextureList* list) {
	if (!list) return;
	for (int i = 0; i < list->count; i++) {
		if (list->entries[i].thumbnail) {
			SDL_DestroyTexture(list->entries[i].thumbnail);
		}
	}
	free(list->entries);
	memset(list, 0, sizeof(*list));
}

int nomos_texture_list_find(const NomosTextureList* list, const char* name) {
	if (!list || !name) return -1;
	for (int i = 0; i < list->count; i++) {
		if (strcmp(list->entries[i].name, name) == 0) {
			return i;
		}
	}
	return -1;
}
