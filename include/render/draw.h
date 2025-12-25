#pragma once

#include <stdint.h>

#include "render/framebuffer.h"

void draw_clear(Framebuffer* fb, uint32_t rgba);
void draw_rect(Framebuffer* fb, int x, int y, int w, int h, uint32_t rgba);
void draw_line(Framebuffer* fb, int x0, int y0, int x1, int y1, uint32_t rgba);

// Alpha-blended solid rect. Color is ABGR8888 (same layout as Framebuffer pixels).
// For each pixel: dst = src * a + dst * (1-a).
void draw_rect_abgr8888_alpha(Framebuffer* fb, int x, int y, int w, int h, uint32_t abgr);

// Blit RGBA8888 pixels into the framebuffer at (dst_x,dst_y).
// Source pixels are expected in the same ABGR/RGBA 32-bit layout as Framebuffer.
void draw_blit_rgba8888(Framebuffer* fb, int dst_x, int dst_y, const uint32_t* src_pixels, int src_w, int src_h);

// Alpha blend ABGR8888 pixels into the framebuffer at (dst_x,dst_y).
// For each pixel: dst = src * a + dst * (1-a).
void draw_blit_abgr8888_alpha(Framebuffer* fb, int dst_x, int dst_y, const uint32_t* src_pixels, int src_w, int src_h);

// Nearest-neighbor scale+blit ABGR8888 pixels into the framebuffer.
// dst rect is in framebuffer pixels.
void draw_blit_abgr8888_scaled_nearest(
	Framebuffer* fb,
	int dst_x,
	int dst_y,
	int dst_w,
	int dst_h,
	const uint32_t* src_pixels,
	int src_w,
	int src_h
);
