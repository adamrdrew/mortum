#pragma once

#include "game/game_state.h"
#include "game/player.h"
#include "game/font.h"
#include "render/framebuffer.h"

#include "assets/asset_paths.h"
#include "render/texture.h"

void hud_draw(FontSystem* font, Framebuffer* fb, const Player* player, const GameState* state, int fps, TextureRegistry* texreg, const AssetPaths* paths);
