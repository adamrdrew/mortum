#pragma once

#include "game/game_state.h"
#include "game/player.h"
#include "render/framebuffer.h"

void hud_draw(Framebuffer* fb, const Player* player, const GameState* state, int fps);
