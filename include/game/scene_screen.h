#pragma once

#include "assets/scene.h"
#include "game/screen.h"

// Creates a Scene-backed screen. Takes ownership of `scene` (moves it).
Screen* scene_screen_create(Scene scene);
