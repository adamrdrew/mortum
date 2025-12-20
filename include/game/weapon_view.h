#pragma once

#include "assets/asset_paths.h"
#include "game/player.h"
#include "render/framebuffer.h"
#include "render/texture.h"

// Draw the first-person weapon viewmodel (currently IDLE only).
// Intended to be rendered before HUD, so the HUD can obscure its bottom edge.
void weapon_view_draw(Framebuffer* fb, const Player* player, TextureRegistry* texreg, const AssetPaths* paths);
