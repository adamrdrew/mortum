#pragma once

#include <stdint.h>

#include "render/framebuffer.h"

void font_draw_text(Framebuffer* fb, int x, int y, const char* text, uint32_t rgba);
