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
// If `sky_filename` is non-NULL and a sector has `ceil_tex` set to "SKY", the ceiling
// is rendered as a DOOM-style cylindrical sky panorama loaded from `Assets/Images/Sky/`.
void raycast_render_textured(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth
);

// Like raycast_render_textured, but uses the provided sector index as the recursion start sector.
// Pass -1 to fall back to automatic sector selection from (cam->x, cam->y).
void raycast_render_textured_from_sector(
	Framebuffer* fb,
	const World* world,
	const Camera* cam,
	TextureRegistry* texreg,
	const AssetPaths* paths,
	const char* sky_filename,
	float* out_depth,
	int start_sector
);
