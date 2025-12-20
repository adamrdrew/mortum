#pragma once

#include <stdbool.h>

#include "platform/window.h"
#include "render/framebuffer.h"

typedef struct Presenter {
	void* texture; // SDL_Texture* stored opaquely
} Presenter;

bool present_init(Presenter* self, Window* window, const Framebuffer* fb);
void present_shutdown(Presenter* self);

bool present_frame(Presenter* self, Window* window, const Framebuffer* fb);
