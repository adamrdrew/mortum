#pragma once

#include "render/framebuffer.h"

// Clamps the framebuffer's RGB colors to a fixed 256-color VGA palette.
// Operates in-place on the full screen (including UI).
void vga_palette_apply(Framebuffer* fb);
