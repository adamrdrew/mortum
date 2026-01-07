// Nomos Studio - SDL2 Font rendering using stb_truetype
#ifndef NOMOS_FONT_H
#define NOMOS_FONT_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#include "assets/asset_paths.h"

#define NOMOS_FONT_GLYPH_COUNT 256

typedef struct NomosGlyph {
	bool valid;
	bool has_bitmap;
	int x, y, w, h;      // Position in atlas
	int xoff, yoff;      // Offset when drawing
	int advance;         // Horizontal advance
} NomosGlyph;

typedef struct NomosFont {
	SDL_Texture* atlas;  // Alpha texture with all glyphs
	int atlas_w, atlas_h;
	int line_height;
	int ascent;
	NomosGlyph glyphs[NOMOS_FONT_GLYPH_COUNT];
	float ui_scale;      // HiDPI scale factor
} NomosFont;

// Initialize font from a TTF file
bool nomos_font_init(NomosFont* font, SDL_Renderer* renderer, const AssetPaths* paths, 
	const char* ttf_filename, int pixel_height, float ui_scale);

// Destroy font resources
void nomos_font_destroy(NomosFont* font);

// Draw text at position (x, y) with color
void nomos_font_draw(NomosFont* font, SDL_Renderer* renderer, int x, int y, 
	const char* text, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

// Measure text width in pixels
int nomos_font_measure_width(NomosFont* font, const char* text);

// Get line height
int nomos_font_line_height(NomosFont* font);

#endif // NOMOS_FONT_H
