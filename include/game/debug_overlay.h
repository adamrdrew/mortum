#pragma once

#include <stdbool.h>

#include "game/font.h"
#include "game/entities.h"
#include "game/player.h"
#include "game/world.h"
#include "render/framebuffer.h"

void debug_overlay_draw(FontSystem* font, Framebuffer* fb, const Player* player, const World* world, const EntitySystem* entities, int fps);
