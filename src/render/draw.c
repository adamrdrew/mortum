#include "render/draw.h"

static int clampi(int v, int lo, int hi) {
	if (v < lo) {
		return lo;
	}
	if (v > hi) {
		return hi;
	}
	return v;
}

void draw_clear(Framebuffer* fb, uint32_t rgba) {
	for (int y = 0; y < fb->height; y++) {
		for (int x = 0; x < fb->width; x++) {
			fb->pixels[y * fb->width + x] = rgba;
		}
	}
}

void draw_rect(Framebuffer* fb, int x, int y, int w, int h, uint32_t rgba) {
	int x0 = clampi(x, 0, fb->width);
	int y0 = clampi(y, 0, fb->height);
	int x1 = clampi(x + w, 0, fb->width);
	int y1 = clampi(y + h, 0, fb->height);
	for (int yy = y0; yy < y1; yy++) {
		for (int xx = x0; xx < x1; xx++) {
			fb->pixels[yy * fb->width + xx] = rgba;
		}
	}
}

static void put_pixel(Framebuffer* fb, int x, int y, uint32_t rgba) {
	if ((unsigned)x >= (unsigned)fb->width || (unsigned)y >= (unsigned)fb->height) {
		return;
	}
	fb->pixels[y * fb->width + x] = rgba;
}

void draw_line(Framebuffer* fb, int x0, int y0, int x1, int y1, uint32_t rgba) {
	// Integer Bresenham.
	int dx = x1 - x0;
	int dy = y1 - y0;
	int sx = (dx >= 0) ? 1 : -1;
	int sy = (dy >= 0) ? 1 : -1;
	dx = dx >= 0 ? dx : -dx;
	dy = dy >= 0 ? dy : -dy;

	int err = (dx > dy ? dx : -dy) / 2;
	for (;;) {
		put_pixel(fb, x0, y0, rgba);
		if (x0 == x1 && y0 == y1) {
			break;
		}
		int e2 = err;
		if (e2 > -dx) {
			err -= dy;
			x0 += sx;
		}
		if (e2 < dy) {
			err += dx;
			y0 += sy;
		}
	}
}

void draw_blit_rgba8888(Framebuffer* fb, int dst_x, int dst_y, const uint32_t* src_pixels, int src_w, int src_h) {
	if (!src_pixels || src_w <= 0 || src_h <= 0) {
		return;
	}

	int x0 = dst_x;
	int y0 = dst_y;
	int x1 = dst_x + src_w;
	int y1 = dst_y + src_h;

	int clip_x0 = clampi(x0, 0, fb->width);
	int clip_y0 = clampi(y0, 0, fb->height);
	int clip_x1 = clampi(x1, 0, fb->width);
	int clip_y1 = clampi(y1, 0, fb->height);

	int src_off_x = clip_x0 - x0;
	int src_off_y = clip_y0 - y0;

	for (int y = clip_y0; y < clip_y1; y++) {
		int sy = (y - clip_y0) + src_off_y;
		const uint32_t* src_row = &src_pixels[sy * src_w + src_off_x];
		uint32_t* dst_row = &fb->pixels[y * fb->width + clip_x0];
		int count = clip_x1 - clip_x0;
		for (int x = 0; x < count; x++) {
			dst_row[x] = src_row[x];
		}
	}
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

void draw_blit_abgr8888_alpha(Framebuffer* fb, int dst_x, int dst_y, const uint32_t* src_pixels, int src_w, int src_h) {
	if (!src_pixels || src_w <= 0 || src_h <= 0) {
		return;
	}

	int x0 = dst_x;
	int y0 = dst_y;
	int x1 = dst_x + src_w;
	int y1 = dst_y + src_h;

	int clip_x0 = clampi(x0, 0, fb->width);
	int clip_y0 = clampi(y0, 0, fb->height);
	int clip_x1 = clampi(x1, 0, fb->width);
	int clip_y1 = clampi(y1, 0, fb->height);

	int src_off_x = clip_x0 - x0;
	int src_off_y = clip_y0 - y0;

	for (int y = clip_y0; y < clip_y1; y++) {
		int sy = (y - clip_y0) + src_off_y;
		const uint32_t* src_row = &src_pixels[sy * src_w + src_off_x];
		uint32_t* dst_row = &fb->pixels[y * fb->width + clip_x0];
		int count = clip_x1 - clip_x0;
		for (int x = 0; x < count; x++) {
			dst_row[x] = blend_abgr8888_over(src_row[x], dst_row[x]);
		}
	}
}
