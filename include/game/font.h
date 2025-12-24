#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "assets/asset_paths.h"
#include "render/framebuffer.h"

typedef struct ColorRGBA {
	uint8_t r, g, b, a;
} ColorRGBA;

static inline ColorRGBA color_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	ColorRGBA c;
	c.r = r;
	c.g = g;
	c.b = b;
	c.a = a;
	return c;
}

typedef struct FontGlyph {
	bool valid;
	bool has_bitmap;
	uint8_t page;
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
	int16_t xoff;
	int16_t yoff;
	int16_t advance_px;
} FontGlyph;

typedef struct FontAtlasPage {
	int w;
	int h;
	int cursor_x;
	int cursor_y;
	int row_h;
	uint8_t* alpha; // owned; 1 byte per pixel
} FontAtlasPage;

typedef struct FontSystem {
	// Owned TTF bytes. Must live for stbtt_fontinfo.
	uint8_t* ttf_data;
	size_t ttf_size;

	// stb_truetype state (opaque here; defined in .c via forward decl).
	// We store it as raw bytes to keep the header small.
	uint8_t stbtt_fontinfo_storage[256];
	float base_scale;
	int base_px;
	int ascent;
	int descent;
	int line_gap;

	FontAtlasPage* pages; // owned
	int page_count;
	int page_cap;

	FontGlyph glyphs[256]; // byte-based glyph cache
} FontSystem;

bool font_system_init(FontSystem* fs, const char* ttf_path, int pixel_height, int atlas_w, int atlas_h, const AssetPaths* paths);
void font_system_shutdown(FontSystem* fs);

void font_draw_text(FontSystem* fs, Framebuffer* fb, int x_px, int y_px, const char* text, ColorRGBA color, float scale);
int font_measure_text_width(FontSystem* fs, const char* text, float scale);
int font_line_height(FontSystem* fs, float scale);

void font_get_stats(FontSystem* fs, int* out_glyphs_cached, int* out_pages, int* out_atlas_w, int* out_atlas_h);

// Draws a small smoke-test page (ASCII sample, scales, colors) starting at (x,y).
void font_draw_test_page(FontSystem* fs, Framebuffer* fb, int x_px, int y_px);
