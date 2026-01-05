#include "render/vga_palette.h"

#include <stddef.h>

static inline uint8_t abgr_a(uint32_t abgr) {
	return (uint8_t)((abgr >> 24) & 0xFFu);
}

static inline uint8_t abgr_b(uint32_t abgr) {
	return (uint8_t)((abgr >> 16) & 0xFFu);
}

static inline uint8_t abgr_g(uint32_t abgr) {
	return (uint8_t)((abgr >> 8) & 0xFFu);
}

static inline uint8_t abgr_r(uint32_t abgr) {
	return (uint8_t)(abgr & 0xFFu);
}

static inline uint32_t abgr_pack(uint8_t a, uint8_t r, uint8_t g, uint8_t b) {
	return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
}

static inline int dist2_rgb(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1) {
	int dr = (int)r0 - (int)r1;
	int dg = (int)g0 - (int)g1;
	int db = (int)b0 - (int)b1;
	return dr * dr + dg * dg + db * db;
}

static void build_vga256_palette(uint8_t pal[256][3]) {
	// Common VGA/DOS 256-color palette layout:
	// - 0..15: EGA system colors
	// - 16..231: 6x6x6 color cube (0,51,102,153,204,255)
	// - 232..255: 24-step grayscale ramp
	static const uint8_t ega16[16][3] = {
		{0, 0, 0},
		{0, 0, 170},
		{0, 170, 0},
		{0, 170, 170},
		{170, 0, 0},
		{170, 0, 170},
		{170, 85, 0},
		{170, 170, 170},
		{85, 85, 85},
		{85, 85, 255},
		{85, 255, 85},
		{85, 255, 255},
		{255, 85, 85},
		{255, 85, 255},
		{255, 255, 85},
		{255, 255, 255},
	};

	for (int i = 0; i < 16; i++) {
		pal[i][0] = ega16[i][0];
		pal[i][1] = ega16[i][1];
		pal[i][2] = ega16[i][2];
	}

	int idx = 16;
	for (int r6 = 0; r6 < 6; r6++) {
		for (int g6 = 0; g6 < 6; g6++) {
			for (int b6 = 0; b6 < 6; b6++) {
				pal[idx][0] = (uint8_t)(r6 * 51);
				pal[idx][1] = (uint8_t)(g6 * 51);
				pal[idx][2] = (uint8_t)(b6 * 51);
				idx++;
			}
		}
	}

	for (int i = 0; i < 24; i++) {
		uint8_t v = (uint8_t)((i * 255 + 11) / 23);
		pal[232 + i][0] = v;
		pal[232 + i][1] = v;
		pal[232 + i][2] = v;
	}
}

static inline uint8_t expand5(uint8_t v5) {
	// 0..31 -> 0..255
	return (uint8_t)((v5 << 3) | (v5 >> 2));
}

static uint32_t g_vga_lut_abgr[1u << 15];
static bool g_vga_lut_ready = false;

static void ensure_vga_lut(void) {
	if (g_vga_lut_ready) {
		return;
	}

	uint8_t pal[256][3];
	build_vga256_palette(pal);

	for (unsigned idx = 0; idx < (1u << 15); idx++) {
		uint8_t r5 = (uint8_t)((idx >> 10) & 31u);
		uint8_t g5 = (uint8_t)((idx >> 5) & 31u);
		uint8_t b5 = (uint8_t)(idx & 31u);
		uint8_t r = expand5(r5);
		uint8_t g = expand5(g5);
		uint8_t b = expand5(b5);

		int best_d = 0x7FFFFFFF;
		uint8_t best_r = 0, best_g = 0, best_b = 0;
		for (int p = 0; p < 256; p++) {
			uint8_t pr = pal[p][0];
			uint8_t pg = pal[p][1];
			uint8_t pb = pal[p][2];
			int d = dist2_rgb(r, g, b, pr, pg, pb);
			if (d < best_d) {
				best_d = d;
				best_r = pr;
				best_g = pg;
				best_b = pb;
				if (d == 0) {
					break;
				}
			}
		}
		g_vga_lut_abgr[idx] = abgr_pack(0xFFu, best_r, best_g, best_b);
	}

	g_vga_lut_ready = true;
}

void vga_palette_apply(Framebuffer* fb) {
	if (!fb || !fb->pixels || fb->width <= 0 || fb->height <= 0) {
		return;
	}

	ensure_vga_lut();

	size_t count = (size_t)fb->width * (size_t)fb->height;
	for (size_t i = 0; i < count; i++) {
		uint32_t p = fb->pixels[i];
		uint8_t a = abgr_a(p);
		uint8_t r5 = (uint8_t)(abgr_r(p) >> 3);
		uint8_t g5 = (uint8_t)(abgr_g(p) >> 3);
		uint8_t b5 = (uint8_t)(abgr_b(p) >> 3);
		unsigned key = ((unsigned)r5 << 10) | ((unsigned)g5 << 5) | (unsigned)b5;
		uint32_t q = g_vga_lut_abgr[key];
		fb->pixels[i] = (q & 0x00FFFFFFu) | ((uint32_t)a << 24);
	}
}
