#pragma once

#include <stdint.h>

// Data-driven Menu asset (loaded from Assets/Menus/*.json).
// Theme is defined only at the root menu; views/submenus only define text + actions.

typedef struct MenuRGBA8 {
	uint8_t r, g, b, a;
} MenuRGBA8;

typedef struct MenuSfxTheme {
	char* on_move_wav;   // owned; filename under Assets/Sounds/Menus/
	char* on_select_wav; // owned
	char* on_back_wav;   // owned
} MenuSfxTheme;

typedef struct MenuTheme {
	char* background_png; // owned; filename under Assets/Images/Menus/Backgrounds/
	char* cursor_png;     // owned; filename under Assets/Images/Menus/Cursors/ (optional; if NULL/missing, runtime uses fallback glyph)
	char* font_ttf;       // owned; filename under Assets/Fonts/
	char* music_midi;     // owned; filename under Assets/Sounds/MIDI/ (optional)
	int text_size_px;
	MenuRGBA8 text_color;
	MenuSfxTheme sfx;
} MenuTheme;

typedef enum MenuActionKind {
	MENU_ACTION_NONE = 0,
	MENU_ACTION_COMMAND = 1,
	MENU_ACTION_SUBMENU = 2,
	MENU_ACTION_CLOSE = 3,
} MenuActionKind;

typedef struct MenuAction {
	MenuActionKind kind;
	char* command;     // owned; kind==COMMAND
	char** args;       // owned array of owned strings; kind==COMMAND
	int arg_count;
	char* submenu_id;  // owned; kind==SUBMENU
} MenuAction;

typedef struct MenuItem {
	char* label;   // owned
	MenuAction action;
} MenuItem;

typedef struct MenuView {
	char* id;      // owned (key in views object)
	char* title;   // owned (optional)
	MenuItem* items; // owned array
	int item_count;
} MenuView;

typedef struct MenuAsset {
	char* name; // owned
	MenuTheme theme;
	MenuView* views; // owned array
	int view_count;
	int root_view_index;
} MenuAsset;

void menu_asset_destroy(MenuAsset* self);

// Returns view index for id, or -1.
int menu_asset_find_view(const MenuAsset* self, const char* id);
