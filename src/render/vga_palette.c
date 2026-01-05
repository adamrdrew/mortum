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

static inline int clampi(int v, int lo, int hi) {
	if (v < lo) return lo;
	if (v > hi) return hi;
	return v;
}

static inline int dist2_rgb(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1) {
	int dr = (int)r0 - (int)r1;
	int dg = (int)g0 - (int)g1;
	int db = (int)b0 - (int)b1;
	return dr * dr + dg * dg + db * db;
}

static void nearest_vga256(uint8_t r, uint8_t g, uint8_t b, uint8_t* out_r, uint8_t* out_g, uint8_t* out_b) {
	// VGA 256-color palette (common layout):
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

	// Candidate 1: 6x6x6 cube.
	int r6 = clampi(((int)r + 25) / 51, 0, 5);
	int g6 = clampi(((int)g + 25) / 51, 0, 5);
	int b6 = clampi(((int)b + 25) / 51, 0, 5);
	uint8_t rc = (uint8_t)(r6 * 51);
	uint8_t gc = (uint8_t)(g6 * 51);
	uint8_t bc = (uint8_t)(b6 * 51);
	int best_d = dist2_rgb(r, g, b, rc, gc, bc);
	uint8_t best_r = rc;
	uint8_t best_g = gc;
	uint8_t best_b = bc;

	// Candidate 2: grayscale ramp (24 levels).
	int y = (299 * (int)r + 587 * (int)g + 114 * (int)b + 500) / 1000;
	int gi = clampi((y * 23 + 127) / 255, 0, 23);
	uint8_t gv = (uint8_t)((gi * 255 + 11) / 23);
	int d_gray = dist2_rgb(r, g, b, gv, gv, gv);
	if (d_gray < best_d) {
		best_d = d_gray;
		best_r = gv;
		best_g = gv;
		best_b = gv;
	}

	// Candidate 3: EGA 16 system colors.
	for (int i = 0; i < 16; i++) {
		uint8_t pr = ega16[i][0];
		uint8_t pg = ega16[i][1];
		uint8_t pb = ega16[i][2];
		int d = dist2_rgb(r, g, b, pr, pg, pb);
		if (d < best_d) {
			best_d = d;
			best_r = pr;
			best_g = pg;
			best_b = pb;
		}
	}

	*out_r = best_r;
	*out_g = best_g;
	*out_b = best_b;
}

void vga_palette_apply(Framebuffer* fb) {
	if (!fb || !fb->pixels || fb->width <= 0 || fb->height <= 0) {
		return;
	}

	size_t count = (size_t)fb->width * (size_t)fb->height;
	for (size_t i = 0; i < count; i++) {
		uint32_t p = fb->pixels[i];
		uint8_t a = abgr_a(p);
		uint8_t r = abgr_r(p);
		uint8_t g = abgr_g(p);
		uint8_t b = abgr_b(p);

		uint8_t qr = 0, qg = 0, qb = 0;
		nearest_vga256(r, g, b, &qr, &qg, &qb);
		fb->pixels[i] = abgr_pack(a, qr, qg, qb);
	}
}
