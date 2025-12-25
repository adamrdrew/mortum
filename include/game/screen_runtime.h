#pragma once

#include <stdbool.h>

#include "game/screen.h"

typedef struct ScreenRuntime {
	Screen* active;
} ScreenRuntime;

void screen_runtime_init(ScreenRuntime* self);
void screen_runtime_shutdown(ScreenRuntime* self, const ScreenContext* ctx);

bool screen_runtime_is_active(const ScreenRuntime* self);

// Takes ownership of `screen`.
void screen_runtime_set(ScreenRuntime* self, Screen* screen, const ScreenContext* ctx);

// Updates active screen; returns true if the screen completed this frame.
bool screen_runtime_update(ScreenRuntime* self, const ScreenContext* ctx, double dt_s);

void screen_runtime_draw(ScreenRuntime* self, const ScreenContext* ctx);
