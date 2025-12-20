#pragma once

#include "game/entity.h"
#include "render/camera.h"
#include "render/framebuffer.h"

#include "render/texture.h"

#include "assets/asset_paths.h"

// Draws billboards for active entities using a depth buffer for occlusion.
//
// If `texreg` and `paths` are provided, this attempts to sample 64x64 tiles from
// `Assets/Images/sprites.bmp` (color-keyed with 0xFFFF00FF).
//
// `depth` is per-screen-column wall distance (same space as raycaster's corrected distance).
// If `depth` is NULL, entities are drawn without occlusion.
void render_entities_billboard(Framebuffer* fb, const Camera* cam, const EntityList* entities, const float* depth, TextureRegistry* texreg, const AssetPaths* paths);
