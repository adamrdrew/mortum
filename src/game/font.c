#include "game/font.h"

#include "core/log.h"
#include "render/draw.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb/stb_truetype.h"

_Static_assert(sizeof(stbtt_fontinfo) <= 256, "FontSystem.stbtt_fontinfo_storage too small");

static char* dup_cstr_local(const char* s) {
	if (!s) {
		return NULL;
	}
	size_t n = strlen(s);
	char* out = (char*)malloc(n + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, s, n + 1);
	return out;
}

static bool is_abs_path(const char* p) {
	return p && p[0] == '/';
}

static bool starts_with(const char* s, const char* prefix) {
	if (!s || !prefix) {
		return false;
	}
	size_t n = strlen(prefix);
	return strncmp(s, prefix, n) == 0;
}

static char* join2(const char* a, const char* b) {
	if (!a || !b) {
		return NULL;
	}
	size_t na = strlen(a);
	size_t nb = strlen(b);
	bool need_slash = (na > 0 && a[na - 1] != '/');
	char* out = (char*)malloc(na + (need_slash ? 1 : 0) + nb + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, a, na);
	size_t off = na;
	if (need_slash) {
		out[off++] = '/';
	}
	memcpy(out + off, b, nb);
	out[off + nb] = '\0';
	return out;
}

static char* resolve_font_path(const AssetPaths* paths, const char* ttf_path) {
	if (!ttf_path || ttf_path[0] == '\0') {
		return NULL;
	}
	if (is_abs_path(ttf_path)) {
		return dup_cstr_local(ttf_path);
	}
	if (starts_with(ttf_path, "Assets/")) {
		if (paths && paths->assets_root) {
			return join2(paths->assets_root, ttf_path + strlen("Assets/"));
		}
		return dup_cstr_local(ttf_path);
	}
	if (paths && paths->assets_root) {
		return join2(paths->assets_root, ttf_path);
	}
	return dup_cstr_local(ttf_path);
}

static bool read_entire_file(const char* path, uint8_t** out_bytes, size_t* out_size) {
	if (!out_bytes || !out_size) {
		return false;
	}
	*out_bytes = NULL;
	*out_size = 0;
	if (!path || path[0] == '\0') {
		return false;
	}
	FILE* f = fopen(path, "rb");
	if (!f) {
		return false;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return false;
	}
	long len = ftell(f);
	if (len <= 0) {
		fclose(f);
		return false;
	}
	if (fseek(f, 0, SEEK_SET) != 0) {
		fclose(f);
		return false;
	}
	uint8_t* data = (uint8_t*)malloc((size_t)len);
	if (!data) {
		fclose(f);
		return false;
	}
	size_t rd = fread(data, 1, (size_t)len, f);
	fclose(f);
	if (rd != (size_t)len) {
		free(data);
		return false;
	}
	*out_bytes = data;
	*out_size = (size_t)len;
	return true;
}

static stbtt_fontinfo* fontinfo(FontSystem* fs) {
	return (stbtt_fontinfo*)fs->stbtt_fontinfo_storage;
}

static void atlas_page_destroy(FontAtlasPage* p) {
	if (!p) {
		return;
	}
	free(p->alpha);
	p->alpha = NULL;
	p->w = p->h = 0;
	p->cursor_x = p->cursor_y = 0;
	p->row_h = 0;
}

static bool atlas_page_init(FontAtlasPage* p, int w, int h) {
	if (!p || w <= 0 || h <= 0) {
		return false;
	}
	memset(p, 0, sizeof(*p));
	p->w = w;
	p->h = h;
	p->cursor_x = 0;
	p->cursor_y = 0;
	p->row_h = 0;
	p->alpha = (uint8_t*)calloc((size_t)w * (size_t)h, 1);
	return p->alpha != NULL;
}

static bool font_ensure_page(FontSystem* fs, int min_w, int min_h, uint8_t* out_page, int* out_x, int* out_y) {
	if (!fs || !out_page || !out_x || !out_y || min_w <= 0 || min_h <= 0) {
		return false;
	}

	// Try to pack into existing pages.
	for (int pi = 0; pi < fs->page_count; pi++) {
		FontAtlasPage* p = &fs->pages[pi];
		int x = p->cursor_x;
		int y = p->cursor_y;
		int row_h = p->row_h;

		if (x + min_w > p->w) {
			// New shelf row.
			y += row_h;
			x = 0;
			row_h = 0;
		}
		if (y + min_h > p->h) {
			continue;
		}

		// Commit
		*out_page = (uint8_t)pi;
		*out_x = x;
		*out_y = y;
		p->cursor_x = x + min_w;
		if (min_h > row_h) {
			row_h = min_h;
		}
		p->row_h = row_h;
		p->cursor_y = y;
		return true;
	}

	// Need a new page.
	if (fs->page_count == fs->page_cap) {
		int next_cap = fs->page_cap > 0 ? fs->page_cap * 2 : 2;
		FontAtlasPage* next = (FontAtlasPage*)realloc(fs->pages, (size_t)next_cap * sizeof(FontAtlasPage));
		if (!next) {
			return false;
		}
		fs->pages = next;
		fs->page_cap = next_cap;
	}

	FontAtlasPage* np = &fs->pages[fs->page_count];
	// Use same size as first page.
	int w = fs->pages[0].w;
	int h = fs->pages[0].h;
	if (w < min_w || h < min_h) {
		return false;
	}
	if (!atlas_page_init(np, w, h)) {
		return false;
	}
	int pi = fs->page_count;
	fs->page_count++;
	// Deterministic: always place at (0,0).
	np->cursor_x = min_w;
	np->cursor_y = 0;
	np->row_h = min_h;
	*out_page = (uint8_t)pi;
	*out_x = 0;
	*out_y = 0;
	return true;
}

static bool font_cache_glyph(FontSystem* fs, uint8_t codepoint) {
	if (!fs) {
		return false;
	}
	FontGlyph* g = &fs->glyphs[codepoint];
	if (g->valid) {
		return true;
	}

	stbtt_fontinfo* info = fontinfo(fs);
	int adv = 0, lsb = 0;
	stbtt_GetCodepointHMetrics(info, (int)codepoint, &adv, &lsb);
	float sc = fs->base_scale;
	int advance_px = (int)lroundf((float)adv * sc);

	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	stbtt_GetCodepointBitmapBox(info, (int)codepoint, sc, sc, &x0, &y0, &x1, &y1);
	int gw = x1 - x0;
	int gh = y1 - y0;

	memset(g, 0, sizeof(*g));
	g->valid = true;
	g->xoff = (int16_t)x0;
	g->yoff = (int16_t)y0;
	g->advance_px = (int16_t)advance_px;

	if (gw <= 0 || gh <= 0) {
		g->has_bitmap = false;
		return true;
	}

	// 1px padding border on all sides.
	int pack_w = gw + 2;
	int pack_h = gh + 2;
	uint8_t page = 0;
	int px = 0, py = 0;
	if (!font_ensure_page(fs, pack_w, pack_h, &page, &px, &py)) {
		// Mark as valid but without bitmap.
		g->has_bitmap = false;
		return true;
	}

	FontAtlasPage* p = &fs->pages[page];
	int dst_x = px + 1;
	int dst_y = py + 1;

	// Render directly into atlas.
	uint8_t* dst = &p->alpha[dst_y * p->w + dst_x];
	stbtt_MakeCodepointBitmap(info, dst, gw, gh, p->w, sc, sc, (int)codepoint);

	g->has_bitmap = true;
	g->page = page;
	g->x = (uint16_t)dst_x;
	g->y = (uint16_t)dst_y;
	g->w = (uint16_t)gw;
	g->h = (uint16_t)gh;
	return true;
}

static inline uint32_t abgr8888(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
	return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline uint32_t blend_abgr8888_over(uint32_t src, uint32_t dst) {
	unsigned a = (unsigned)(src >> 24) & 0xFFu;
	if (a == 0u) {
		return dst;
	}
	if (a == 255u) {
		return src;
	}

	unsigned inv_a = 255u - a;
	unsigned src_r = (unsigned)(src)&0xFFu;
	unsigned src_g = (unsigned)(src >> 8) & 0xFFu;
	unsigned src_b = (unsigned)(src >> 16) & 0xFFu;

	unsigned dst_r = (unsigned)(dst)&0xFFu;
	unsigned dst_g = (unsigned)(dst >> 8) & 0xFFu;
	unsigned dst_b = (unsigned)(dst >> 16) & 0xFFu;

	unsigned out_r = (src_r * a + dst_r * inv_a + 127u) / 255u;
	unsigned out_g = (src_g * a + dst_g * inv_a + 127u) / 255u;
	unsigned out_b = (src_b * a + dst_b * inv_a + 127u) / 255u;

	return (0xFFu << 24) | (out_b << 16) | (out_g << 8) | out_r;
}

static void blit_alpha_scaled(Framebuffer* fb, int dst_x, int dst_y, const uint8_t* src, int src_w, int src_h, int src_stride, ColorRGBA color, float scale) {
	if (!fb || !fb->pixels || !src || src_w <= 0 || src_h <= 0 || src_stride <= 0) {
		return;
	}
	if (scale <= 0.0f) {
		return;
	}

	// Special-case scale=1 (common).
	if (fabsf(scale - 1.0f) < 0.0001f) {
		for (int y = 0; y < src_h; y++) {
			int yy = dst_y + y;
			if ((unsigned)yy >= (unsigned)fb->height) {
				continue;
			}
			const uint8_t* srow = &src[y * src_stride];
			uint32_t* drow = &fb->pixels[yy * fb->width];
			for (int x = 0; x < src_w; x++) {
				int xx = dst_x + x;
				if ((unsigned)xx >= (unsigned)fb->width) {
					continue;
				}
				uint8_t a0 = srow[x];
				if (a0 == 0) {
					continue;
				}
				unsigned a = (unsigned)a0 * (unsigned)color.a;
				uint8_t a8 = (uint8_t)((a + 127u) / 255u);
				uint32_t src_px = abgr8888(color.r, color.g, color.b, a8);
				drow[xx] = blend_abgr8888_over(src_px, drow[xx]);
			}
		}
		return;
	}

	int dst_w = (int)ceilf((float)src_w * scale);
	int dst_h = (int)ceilf((float)src_h * scale);
	if (dst_w <= 0 || dst_h <= 0) {
		return;
	}

	for (int y = 0; y < dst_h; y++) {
		int yy = dst_y + y;
		if ((unsigned)yy >= (unsigned)fb->height) {
			continue;
		}
		int sy = (int)((float)y / scale);
		if (sy < 0) {
			sy = 0;
		}
		if (sy >= src_h) {
			sy = src_h - 1;
		}
		const uint8_t* srow = &src[sy * src_stride];
		uint32_t* drow = &fb->pixels[yy * fb->width];
		for (int x = 0; x < dst_w; x++) {
			int xx = dst_x + x;
			if ((unsigned)xx >= (unsigned)fb->width) {
				continue;
			}
			int sx = (int)((float)x / scale);
			if (sx < 0) {
				sx = 0;
			}
			if (sx >= src_w) {
				sx = src_w - 1;
			}
			uint8_t a0 = srow[sx];
			if (a0 == 0) {
				continue;
			}
			unsigned a = (unsigned)a0 * (unsigned)color.a;
			uint8_t a8 = (uint8_t)((a + 127u) / 255u);
			uint32_t src_px = abgr8888(color.r, color.g, color.b, a8);
			drow[xx] = blend_abgr8888_over(src_px, drow[xx]);
		}
	}
}

bool font_system_init(FontSystem* fs, const char* ttf_path, int pixel_height, int atlas_w, int atlas_h, const AssetPaths* paths) {
	if (!fs) {
		return false;
	}
	memset(fs, 0, sizeof(*fs));

	if (pixel_height <= 0) {
		log_error("Font: invalid pixel_height=%d", pixel_height);
		return false;
	}
	if (atlas_w < 64 || atlas_h < 64) {
		log_error("Font: invalid atlas size %dx%d", atlas_w, atlas_h);
		return false;
	}

	char* resolved = resolve_font_path(paths, ttf_path);
	if (!resolved) {
		log_error("Font: out of memory resolving path");
		return false;
	}

	uint8_t* bytes = NULL;
	size_t size = 0;
	if (!read_entire_file(resolved, &bytes, &size)) {
		log_error("Font: failed to read TTF: %s", resolved);
		free(resolved);
		return false;
	}
	free(resolved);

	fs->ttf_data = bytes;
	fs->ttf_size = size;
	fs->base_px = pixel_height;

	stbtt_fontinfo* info = fontinfo(fs);
	int off = stbtt_GetFontOffsetForIndex(bytes, 0);
	if (off < 0) {
		log_error("Font: invalid TTF (no font at index 0)");
		font_system_shutdown(fs);
		return false;
	}
	if (!stbtt_InitFont(info, bytes, off)) {
		log_error("Font: stbtt_InitFont failed");
		font_system_shutdown(fs);
		return false;
	}

	fs->base_scale = stbtt_ScaleForPixelHeight(info, (float)pixel_height);
	stbtt_GetFontVMetrics(info, &fs->ascent, &fs->descent, &fs->line_gap);

	fs->pages = (FontAtlasPage*)malloc(sizeof(FontAtlasPage));
	if (!fs->pages) {
		log_error("Font: out of memory allocating atlas pages");
		font_system_shutdown(fs);
		return false;
	}
	fs->page_cap = 1;
	fs->page_count = 1;
	if (!atlas_page_init(&fs->pages[0], atlas_w, atlas_h)) {
		log_error("Font: out of memory allocating atlas %dx%d", atlas_w, atlas_h);
		font_system_shutdown(fs);
		return false;
	}

	// Pre-cache printable ASCII for a smooth first frame.
	for (int c = 32; c < 127; c++) {
		(void)font_cache_glyph(fs, (uint8_t)c);
	}

	return true;
}

void font_system_shutdown(FontSystem* fs) {
	if (!fs) {
		return;
	}
	for (int i = 0; i < fs->page_count; i++) {
		atlas_page_destroy(&fs->pages[i]);
	}
	free(fs->pages);
	fs->pages = NULL;
	fs->page_count = 0;
	fs->page_cap = 0;
	free(fs->ttf_data);
	fs->ttf_data = NULL;
	fs->ttf_size = 0;
	memset(fs->glyphs, 0, sizeof(fs->glyphs));
}

int font_line_height(FontSystem* fs, float scale) {
	if (!fs) {
		return 0;
	}
	if (scale <= 0.0f) {
		scale = 1.0f;
	}
	float sc = fs->base_scale;
	float lh = (float)(fs->ascent - fs->descent + fs->line_gap) * sc;
	return (int)lroundf(lh * scale);
}

int font_measure_text_width(FontSystem* fs, const char* text, float scale) {
	if (!fs || !text) {
		return 0;
	}
	if (scale <= 0.0f) {
		scale = 1.0f;
	}
	stbtt_fontinfo* info = fontinfo(fs);
	float pen = 0.0f;
	float max_w = 0.0f;
	for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
		unsigned char ch = *p;
		if (ch == '\n') {
			if (pen > max_w) {
				max_w = pen;
			}
			pen = 0.0f;
			continue;
		}
		(void)font_cache_glyph(fs, ch);
		pen += (float)fs->glyphs[ch].advance_px * scale;
		if (p[1] != 0 && p[1] != '\n') {
			int kern = stbtt_GetCodepointKernAdvance(info, (int)ch, (int)p[1]);
			pen += (float)lroundf((float)kern * fs->base_scale) * scale;
		}
	}
	if (pen > max_w) {
		max_w = pen;
	}
	return (int)lroundf(max_w);
}

void font_draw_text(FontSystem* fs, Framebuffer* fb, int x_px, int y_px, const char* text, ColorRGBA color, float scale) {
	if (!fs || !fb || !fb->pixels) {
		return;
	}
	if (!text) {
		return;
	}
	if (scale <= 0.0f) {
		scale = 1.0f;
	}

	stbtt_fontinfo* info = fontinfo(fs);
	float sc = fs->base_scale;
	int baseline = y_px + (int)lroundf((float)fs->ascent * sc * scale);
	float pen_x = (float)x_px;
	int line_h = font_line_height(fs, scale);

	for (const unsigned char* p = (const unsigned char*)text; *p; p++) {
		unsigned char ch = *p;
		if (ch == '\n') {
			baseline += line_h;
			pen_x = (float)x_px;
			continue;
		}

		if (!font_cache_glyph(fs, ch)) {
			continue;
		}
		FontGlyph* g = &fs->glyphs[ch];
		if (g->has_bitmap && g->w > 0 && g->h > 0 && (unsigned)g->page < (unsigned)fs->page_count) {
			FontAtlasPage* pg = &fs->pages[g->page];
			const uint8_t* src = &pg->alpha[g->y * pg->w + g->x];
			int dst_x = (int)lroundf(pen_x + (float)g->xoff * scale);
			int dst_y = (int)lroundf((float)baseline + (float)g->yoff * scale);
			blit_alpha_scaled(fb, dst_x, dst_y, src, (int)g->w, (int)g->h, pg->w, color, scale);
		}

		pen_x += (float)g->advance_px * scale;
		if (p[1] != 0 && p[1] != '\n') {
			int kern = stbtt_GetCodepointKernAdvance(info, (int)ch, (int)p[1]);
			pen_x += (float)lroundf((float)kern * sc) * scale;
		}
	}
}

void font_get_stats(FontSystem* fs, int* out_glyphs_cached, int* out_pages, int* out_atlas_w, int* out_atlas_h) {
	if (!fs) {
		if (out_glyphs_cached) {
			*out_glyphs_cached = 0;
		}
		if (out_pages) {
			*out_pages = 0;
		}
		if (out_atlas_w) {
			*out_atlas_w = 0;
		}
		if (out_atlas_h) {
			*out_atlas_h = 0;
		}
		return;
	}
	int n = 0;
	for (int i = 0; i < 256; i++) {
		if (fs->glyphs[i].valid) {
			n++;
		}
	}
	if (out_glyphs_cached) {
		*out_glyphs_cached = n;
	}
	if (out_pages) {
		*out_pages = fs->page_count;
	}
	if (out_atlas_w) {
		*out_atlas_w = (fs->page_count > 0) ? fs->pages[0].w : 0;
	}
	if (out_atlas_h) {
		*out_atlas_h = (fs->page_count > 0) ? fs->pages[0].h : 0;
	}
}

void font_draw_test_page(FontSystem* fs, Framebuffer* fb, int x_px, int y_px) {
	if (!fs || !fb) {
		return;
	}
	int lh = font_line_height(fs, 1.0f);
	int panel_w = fb->width - x_px * 2;
	if (panel_w < 200) {
		panel_w = 200;
	}
	int panel_h = lh * 14;
	draw_rect(fb, x_px - 4, y_px - 4, panel_w, panel_h, 0xCC000000u);

	ColorRGBA white = color_rgba(255, 255, 255, 255);
	ColorRGBA green = color_rgba(144, 255, 144, 255);
	ColorRGBA cyan = color_rgba(144, 224, 255, 255);
	ColorRGBA red = color_rgba(255, 144, 144, 255);

	int glyphs = 0, pages = 0, aw = 0, ah = 0;
	font_get_stats(fs, &glyphs, &pages, &aw, &ah);

	char buf[256];
	snprintf(buf, sizeof(buf), "Font smoke test (ProggyClean)  size=%dpx  glyphs=%d  pages=%d  atlas=%dx%d", fs->base_px, glyphs, pages, aw, ah);
	font_draw_text(fs, fb, x_px, y_px, buf, white, 1.0f);

	int y = y_px + lh;
	font_draw_text(fs, fb, x_px, y, "ASCII: !\"#$%&'()*+,-./ 0123456789 :;<=>? @", green, 1.0f);
	y += lh;
	font_draw_text(fs, fb, x_px, y, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", green, 1.0f);
	y += lh;
	font_draw_text(fs, fb, x_px, y, "abcdefghijklmnopqrstuvwxyz", green, 1.0f);
	y += lh;
	font_draw_text(fs, fb, x_px, y, "[]{}() <> \\ | ~ ^ _ `", cyan, 1.0f);
	y += lh;
	font_draw_text(fs, fb, x_px, y, "Multiple lines:\n  line 2\n  line 3", white, 1.0f);
	y += lh * 3;
	font_draw_text(fs, fb, x_px, y, "Scale 0.75x", white, 0.75f);
	y += font_line_height(fs, 0.75f);
	font_draw_text(fs, fb, x_px, y, "Scale 1.0x", white, 1.0f);
	y += font_line_height(fs, 1.0f);
	font_draw_text(fs, fb, x_px, y, "Scale 1.5x", white, 1.5f);
	y += font_line_height(fs, 1.5f);
	font_draw_text(fs, fb, x_px, y, "Scale 2.0x", white, 2.0f);
	y += font_line_height(fs, 2.0f);

	font_draw_text(fs, fb, x_px, y, "Colors: white / green / cyan / red", white, 1.0f);
	y += lh;
	font_draw_text(fs, fb, x_px, y, "green", green, 1.0f);
	font_draw_text(fs, fb, x_px + font_measure_text_width(fs, "green ", 1.0f), y, "cyan", cyan, 1.0f);
	font_draw_text(fs, fb, x_px + font_measure_text_width(fs, "green cyan ", 1.0f), y, "red", red, 1.0f);
}
