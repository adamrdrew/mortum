#pragma once

#include <stdbool.h>

#include "game/entities.h"
#include "game/player.h"
#include "game/world.h"
#include "render/framebuffer.h"

void debug_overlay_draw(Framebuffer* fb, const Player* player, const World* world, const EntitySystem* entities, int fps);
