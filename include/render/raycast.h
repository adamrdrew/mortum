#pragma once

#include "game/world.h"
#include "render/camera.h"
#include "render/framebuffer.h"

#include "render/texture.h"

// Untextured baseline raycast renderer.
void raycast_render_untextured(Framebuffer* fb, const World* world, const Camera* cam);

// Textured wall path (nearest sampling). Falls back to flat shading if texture missing.
// If `out_depth` is non-NULL, it must be at least fb->width floats and will be filled
// with the corrected wall distance for each column (or a large value if nothing hit).
void raycast_render_textured(Framebuffer* fb, const World* world, const Camera* cam, TextureRegistry* texreg, const AssetPaths* paths, float* out_depth);
