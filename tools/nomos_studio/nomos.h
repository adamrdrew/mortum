// Nomos Studio - Core header with common types and forward declarations
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

// Forward declarations of engine types
typedef struct AssetPaths AssetPaths;
typedef struct MapLoadResult MapLoadResult;
typedef struct EntityDefs EntityDefs;
typedef struct World World;
typedef struct MapValidationReport MapValidationReport;

// Menu actions
typedef enum NomosMenuAction {
	NOMOS_MENU_NONE = 0,
	NOMOS_MENU_OPEN,
	NOMOS_MENU_SAVE,
	NOMOS_MENU_SAVE_AS,
	NOMOS_MENU_VALIDATE,
	NOMOS_MENU_EXIT,
	NOMOS_MENU_GENERATE,
	NOMOS_MENU_RUN,
} NomosMenuAction;

// Palette modes
typedef enum NomosPaletteMode {
	NOMOS_PALETTE_ENTITIES = 0,
	NOMOS_PALETTE_LIGHTS,
	NOMOS_PALETTE_PARTICLES,
	NOMOS_PALETTE_PLAYER_START,
} NomosPaletteMode;

// Selection types
typedef enum NomosSelectionType {
	NOMOS_SEL_NONE = 0,
	NOMOS_SEL_SECTOR,
	NOMOS_SEL_WALL,
	NOMOS_SEL_ENTITY,
	NOMOS_SEL_LIGHT,
	NOMOS_SEL_PARTICLE,
	NOMOS_SEL_PLAYER_START,
} NomosSelectionType;

// Layout regions (calculated each frame)
typedef struct NomosLayout {
	SDL_Rect menu_bar;
	SDL_Rect left_panel;
	SDL_Rect viewport;
	SDL_Rect right_panel;
	SDL_Rect status_bar;
} NomosLayout;

// Texture thumbnail entry
typedef struct NomosTextureEntry {
	char name[64];
	SDL_Texture* thumbnail;
	int original_width;
	int original_height;
} NomosTextureEntry;

// Texture list for browsing
typedef struct NomosTextureList {
	NomosTextureEntry* entries;
	int count;
	int capacity;
} NomosTextureList;

// Entity thumbnail entry
typedef struct NomosEntityEntry {
	uint32_t def_index;
	char name[64];
	SDL_Texture* thumbnail;
	int thumb_width;
	int thumb_height;
} NomosEntityEntry;

// Procgen parameters
typedef struct NomosGenParams {
	int seed;
	int room_count;
	int map_width;
	int map_height;
	float corridor_density;
	int outdoor_pockets;
} NomosGenParams;

// Layout constants
#define NOMOS_MENU_HEIGHT 24
#define NOMOS_LEFT_PANEL_WIDTH 200
#define NOMOS_RIGHT_PANEL_WIDTH 280
#define NOMOS_STATUS_HEIGHT 22

// Colors (RGBA)
#define NOMOS_COLOR_BG_DARK       40, 40, 45, 255
#define NOMOS_COLOR_BG_PANEL      50, 50, 55, 255
#define NOMOS_COLOR_BG_BUTTON     70, 70, 75, 255
#define NOMOS_COLOR_BG_BUTTON_HOV 90, 90, 95, 255
#define NOMOS_COLOR_BG_BUTTON_ACT 60, 120, 180, 255
#define NOMOS_COLOR_TEXT          220, 220, 220, 255
#define NOMOS_COLOR_TEXT_DIM      140, 140, 140, 255
#define NOMOS_COLOR_ACCENT        80, 140, 200, 255
#define NOMOS_COLOR_BORDER        80, 80, 85, 255
#define NOMOS_COLOR_SELECTED      100, 160, 220, 255
#define NOMOS_COLOR_ERROR         220, 80, 80, 255
#define NOMOS_COLOR_WARNING       220, 180, 80, 255

// Viewport colors
#define NOMOS_COLOR_GRID          60, 60, 65, 255
#define NOMOS_COLOR_WALL_SOLID    180, 180, 180, 255
#define NOMOS_COLOR_WALL_PORTAL   100, 180, 100, 255
#define NOMOS_COLOR_WALL_DOOR     200, 160, 80, 255
#define NOMOS_COLOR_SECTOR_FILL   45, 50, 55, 180
#define NOMOS_COLOR_SECTOR_HOVER  60, 80, 100, 180
#define NOMOS_COLOR_PLAYER_START  50, 200, 50, 255
#define NOMOS_COLOR_ENTITY        200, 150, 50, 255
#define NOMOS_COLOR_LIGHT         255, 220, 100, 200
#define NOMOS_COLOR_PARTICLE      150, 100, 200, 200

// Helper macros
#define NOMOS_MAX(a, b) ((a) > (b) ? (a) : (b))
#define NOMOS_MIN(a, b) ((a) < (b) ? (a) : (b))
#define NOMOS_CLAMP(v, lo, hi) NOMOS_MIN(NOMOS_MAX((v), (lo)), (hi))

// Texture list functions
void nomos_texture_list_load(NomosTextureList* list, const AssetPaths* paths, SDL_Renderer* renderer);
void nomos_texture_list_destroy(NomosTextureList* list);
int nomos_texture_list_find(const NomosTextureList* list, const char* name);
