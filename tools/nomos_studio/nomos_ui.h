// Nomos Studio - UI Framework
// A minimal immediate-mode UI toolkit built on SDL2.
#pragma once

#include "nomos.h"
#include "nomos_document.h"

#include <SDL.h>
#include <stdbool.h>

// Dialog types
typedef enum NomosDialogType {
	NOMOS_DIALOG_NONE = 0,
	NOMOS_DIALOG_OPEN,
	NOMOS_DIALOG_SAVE_AS,
	NOMOS_DIALOG_GENERATE,
	NOMOS_DIALOG_ERROR,
	NOMOS_DIALOG_TEXTURE_PICKER,
} NomosDialogType;

// Dialog state
typedef struct NomosDialogState {
	NomosDialogType type;
	NomosMenuAction pending_action;
	
	// For file dialogs
	char input_path[512];
	int input_cursor;
	
	// For generate dialog
	NomosGenParams gen_params;
	
	// For error dialog
	char error_message[256];
	
	// For texture picker
	int texture_scroll;
	int selected_texture;
	char texture_result[64];
	bool texture_confirmed;
	
	// Result
	bool has_result;
	char result_path[512];
} NomosDialogState;

// UI State
typedef struct NomosUI {
	SDL_Renderer* renderer;
	
	// Menu state
	int open_menu;  // -1 = none, 0 = File, 1 = Generate, 2 = Run
	bool menu_active;
	
	// Palette state
	NomosPaletteMode palette_mode;
	int palette_scroll;
	int palette_hovered;
	int palette_selected;
	
	// Inspector scroll
	int inspector_scroll;
	
	// Validation results scroll
	int validation_scroll;
	
	// Texture picker state
	int texture_picker_scroll;
	
	// Mouse state
	int mouse_x;
	int mouse_y;
	bool mouse_down;
	bool mouse_clicked;
	
	// Hover tracking
	bool any_widget_hovered;
	
} NomosUI;

// UI lifecycle
void nomos_ui_init(NomosUI* ui, SDL_Renderer* renderer);
void nomos_ui_destroy(NomosUI* ui);

// Event handling (returns true if event was consumed)
bool nomos_ui_handle_event(NomosUI* ui, SDL_Event* event, NomosDocument* doc, NomosDialogState* dialog);

// Layout calculation
void nomos_ui_calculate_layout(NomosLayout* layout, int window_width, int window_height);

// Rendering
void nomos_ui_render(NomosUI* ui, SDL_Renderer* renderer, const NomosLayout* layout,
	NomosDocument* doc, const EntityDefs* entity_defs, const NomosTextureList* textures,
	NomosDialogState* dialog);

// Dialog functions
void nomos_dialog_init(NomosDialogState* dialog);
void nomos_dialog_destroy(NomosDialogState* dialog);
void nomos_dialog_show_open(NomosDialogState* dialog, const AssetPaths* paths);
void nomos_dialog_show_save_as(NomosDialogState* dialog, const AssetPaths* paths);
void nomos_dialog_show_generate(NomosDialogState* dialog);
void nomos_dialog_show_error(NomosDialogState* dialog, const char* message);
void nomos_dialog_show_texture_picker(NomosDialogState* dialog);
bool nomos_dialog_poll_result(NomosDialogState* dialog, NomosMenuAction* action, char* path_out, size_t path_size);
void nomos_dialog_get_gen_params(NomosDialogState* dialog, NomosGenParams* params);
void nomos_dialog_render(NomosDialogState* dialog, SDL_Renderer* renderer, int window_width, int window_height);
bool nomos_dialog_handle_event(NomosDialogState* dialog, SDL_Event* event);

// Immediate-mode widgets (internal use)
typedef struct NomosWidgetResult {
	bool hovered;
	bool clicked;
	bool value_changed;
} NomosWidgetResult;

NomosWidgetResult nomos_ui_button(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label);
NomosWidgetResult nomos_ui_label(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* text, bool dim);
NomosWidgetResult nomos_ui_checkbox(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label, bool* value);
NomosWidgetResult nomos_ui_slider_int(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label, int* value, int min_val, int max_val);
NomosWidgetResult nomos_ui_slider_float(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, const char* label, float* value, float min_val, float max_val);
NomosWidgetResult nomos_ui_text_input(NomosUI* ui, SDL_Renderer* renderer, SDL_Rect rect, char* buffer, size_t buffer_size, int* cursor);
