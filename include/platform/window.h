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

// Sets borderless fullscreen-on-desktop when `fullscreen=true`, otherwise returns to windowed.
// Returns false on failure (and logs why).
bool window_set_fullscreen(Window* self, bool fullscreen);

// Returns true if the window is currently in fullscreen mode.
bool window_is_fullscreen(const Window* self);

// Sets the window size in windowed mode. (No-op if window is NULL.)
void window_set_size(Window* self, int width, int height);

// Centers the window on screen in windowed mode.
void window_center(Window* self);
