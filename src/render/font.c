#include "render/font.h"

#include "render/draw.h"

// Tiny placeholder: draws each character as a 6x8 block (readable enough for debug).
void font_draw_text(Framebuffer* fb, int x, int y, const char* text, uint32_t rgba) {
	if (!text) {
		return;
	}
	int cx = x;
	for (const char* p = text; *p; p++) {
		if (*p == '\n') {
			y += 10;
			cx = x;
			continue;
		}
		// outline block
		draw_rect(fb, cx, y, 6, 8, rgba);
		cx += 7;
	}
}
