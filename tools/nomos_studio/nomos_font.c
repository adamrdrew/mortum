// Nomos Studio - SDL2 Font rendering using stb_truetype
#include "nomos_font.h"

#include "assets/asset_paths.h"

// Use stb_truetype (implementation already in src/game/font.c)
#include "stb/stb_truetype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ATLAS_SIZE 512

bool nomos_font_init(NomosFont* font, SDL_Renderer* renderer, const AssetPaths* paths,
	const char* ttf_filename, int pixel_height, float ui_scale) {
	if (!font || !renderer || !paths || !ttf_filename) return false;
	
	memset(font, 0, sizeof(*font));
	font->ui_scale = ui_scale > 0 ? ui_scale : 1.0f;
	
	// Scale the pixel height for HiDPI
	int scaled_height = (int)(pixel_height * font->ui_scale);
	
	// Build path to font file
	char* font_path = asset_path_join(paths, "Fonts", ttf_filename);
	if (!font_path) {
		fprintf(stderr, "nomos_font: Failed to build font path\n");
		return false;
	}
	
	// Load TTF file
	FILE* fp = fopen(font_path, "rb");
	if (!fp) {
		fprintf(stderr, "nomos_font: Failed to open font: %s\n", font_path);
		free(font_path);
		return false;
	}
	
	fseek(fp, 0, SEEK_END);
	size_t ttf_size = (size_t)ftell(fp);
	fseek(fp, 0, SEEK_SET);
	
	uint8_t* ttf_data = malloc(ttf_size);
	if (!ttf_data) {
		fclose(fp);
		free(font_path);
		return false;
	}
	
	if (fread(ttf_data, 1, ttf_size, fp) != ttf_size) {
		fclose(fp);
		free(ttf_data);
		free(font_path);
		return false;
	}
	fclose(fp);
	free(font_path);
	
	// Initialize stb_truetype
	stbtt_fontinfo stb_font;
	if (!stbtt_InitFont(&stb_font, ttf_data, stbtt_GetFontOffsetForIndex(ttf_data, 0))) {
		fprintf(stderr, "nomos_font: Failed to init stb_truetype\n");
		free(ttf_data);
		return false;
	}
	
	float scale = stbtt_ScaleForPixelHeight(&stb_font, (float)scaled_height);
	
	int ascent, descent, line_gap;
	stbtt_GetFontVMetrics(&stb_font, &ascent, &descent, &line_gap);
	font->ascent = (int)(ascent * scale);
	font->line_height = (int)((ascent - descent + line_gap) * scale);
	
	// Create atlas
	int atlas_size = ATLAS_SIZE;
	// Scale atlas for HiDPI if needed
	if (font->ui_scale > 1.5f) atlas_size = 1024;
	
	font->atlas_w = atlas_size;
	font->atlas_h = atlas_size;
	
	uint8_t* atlas_pixels = calloc((size_t)(atlas_size * atlas_size), 1);
	if (!atlas_pixels) {
		free(ttf_data);
		return false;
	}
	
	// Pack glyphs into atlas
	int cursor_x = 1;
	int cursor_y = 1;
	int row_height = 0;
	
	for (int c = 32; c < 127; c++) {
		int glyph_w, glyph_h, xoff, yoff;
		uint8_t* glyph_bitmap = stbtt_GetCodepointBitmap(&stb_font, scale, scale, c, &glyph_w, &glyph_h, &xoff, &yoff);
		
		if (!glyph_bitmap) {
			font->glyphs[c].valid = false;
			continue;
		}
		
		// Check if we need to move to next row
		if (cursor_x + glyph_w + 1 > atlas_size) {
			cursor_x = 1;
			cursor_y += row_height + 1;
			row_height = 0;
		}
		
		// Check if we've run out of space
		if (cursor_y + glyph_h + 1 > atlas_size) {
			stbtt_FreeBitmap(glyph_bitmap, NULL);
			continue;
		}
		
		// Copy glyph to atlas
		for (int gy = 0; gy < glyph_h; gy++) {
			for (int gx = 0; gx < glyph_w; gx++) {
				int ax = cursor_x + gx;
				int ay = cursor_y + gy;
				atlas_pixels[ay * atlas_size + ax] = glyph_bitmap[gy * glyph_w + gx];
			}
		}
		
		// Store glyph info
		font->glyphs[c].valid = true;
		font->glyphs[c].has_bitmap = (glyph_w > 0 && glyph_h > 0);
		font->glyphs[c].x = cursor_x;
		font->glyphs[c].y = cursor_y;
		font->glyphs[c].w = glyph_w;
		font->glyphs[c].h = glyph_h;
		font->glyphs[c].xoff = xoff;
		font->glyphs[c].yoff = yoff;
		
		int advance, lsb;
		stbtt_GetCodepointHMetrics(&stb_font, c, &advance, &lsb);
		font->glyphs[c].advance = (int)(advance * scale);
		
		// Update cursor
		cursor_x += glyph_w + 1;
		if (glyph_h > row_height) row_height = glyph_h;
		
		stbtt_FreeBitmap(glyph_bitmap, NULL);
	}
	
	// Handle space specially
	{
		int advance, lsb;
		stbtt_GetCodepointHMetrics(&stb_font, ' ', &advance, &lsb);
		font->glyphs[' '].valid = true;
		font->glyphs[' '].has_bitmap = false;
		font->glyphs[' '].advance = (int)(advance * scale);
	}
	
	free(ttf_data);
	
	// Create SDL texture from atlas
	// We need an ARGB texture where RGB is white and A is the glyph coverage
	// Use ARGB8888 which stores bytes as [A][R][G][B] in memory on little-endian
	uint8_t* rgba_pixels = malloc((size_t)(atlas_size * atlas_size * 4));
	if (!rgba_pixels) {
		free(atlas_pixels);
		return false;
	}
	
	for (int i = 0; i < atlas_size * atlas_size; i++) {
		uint8_t alpha = atlas_pixels[i];
		// ARGB8888 layout: bytes are B, G, R, A (little-endian 0xAARRGGBB)
		rgba_pixels[i * 4 + 0] = 255;  // B
		rgba_pixels[i * 4 + 1] = 255;  // G
		rgba_pixels[i * 4 + 2] = 255;  // R
		rgba_pixels[i * 4 + 3] = alpha; // A
	}
	
	free(atlas_pixels);
	
	font->atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STATIC, atlas_size, atlas_size);
	if (!font->atlas) {
		fprintf(stderr, "nomos_font: Failed to create atlas texture: %s\n", SDL_GetError());
		free(rgba_pixels);
		return false;
	}
	
	SDL_UpdateTexture(font->atlas, NULL, rgba_pixels, atlas_size * 4);
	SDL_SetTextureBlendMode(font->atlas, SDL_BLENDMODE_BLEND);
	
	free(rgba_pixels);
	
	printf("nomos_font: Loaded %s at %dpx (scale %.2f)\n", ttf_filename, scaled_height, font->ui_scale);
	return true;
}

void nomos_font_destroy(NomosFont* font) {
	if (!font) return;
	if (font->atlas) {
		SDL_DestroyTexture(font->atlas);
		font->atlas = NULL;
	}
	memset(font, 0, sizeof(*font));
}

void nomos_font_draw(NomosFont* font, SDL_Renderer* renderer, int x, int y,
	const char* text, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
	if (!font || !font->atlas || !renderer || !text) return;
	
	SDL_SetTextureColorMod(font->atlas, r, g, b);
	SDL_SetTextureAlphaMod(font->atlas, a);
	
	int cx = x;
	int cy = y;
	
	for (const char* p = text; *p; p++) {
		unsigned char c = (unsigned char)*p;
		
		if (c == '\n') {
			cx = x;
			cy += font->line_height;
			continue;
		}
		
		if (!font->glyphs[c].valid) {
			c = '?';
		}
		
		NomosGlyph* g = &font->glyphs[c];
		
		if (g->has_bitmap) {
			SDL_Rect src = {g->x, g->y, g->w, g->h};
			SDL_Rect dst = {cx + g->xoff, cy + font->ascent + g->yoff, g->w, g->h};
			SDL_RenderCopy(renderer, font->atlas, &src, &dst);
		}
		
		cx += g->advance;
	}
}

int nomos_font_measure_width(NomosFont* font, const char* text) {
	if (!font || !text) return 0;
	
	int width = 0;
	int max_width = 0;
	
	for (const char* p = text; *p; p++) {
		unsigned char c = (unsigned char)*p;
		
		if (c == '\n') {
			if (width > max_width) max_width = width;
			width = 0;
			continue;
		}
		
		if (!font->glyphs[c].valid) {
			c = '?';
		}
		
		width += font->glyphs[c].advance;
	}
	
	return width > max_width ? width : max_width;
}

int nomos_font_line_height(NomosFont* font) {
	if (!font) return 16;
	return font->line_height;
}
