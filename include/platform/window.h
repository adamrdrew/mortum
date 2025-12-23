#pragma once

#include <stdbool.h>

typedef struct SDL_Window SDL_Window;
typedef struct SDL_Renderer SDL_Renderer;

typedef struct Window {
	SDL_Window* window;
	SDL_Renderer* renderer;
	int width;
	int height;
} Window;

bool window_create(Window* self, const char* title, int width, int height, bool vsync);
void window_destroy(Window* self);
