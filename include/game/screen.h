#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"
#include "platform/input.h"
#include "render/framebuffer.h"

// Minimal internal Screen interface.
// A Screen is "blocking" in the sense that it stays active until it returns DONE.

typedef enum ScreenResult {
	SCREEN_RESULT_RUNNING = 0,
	SCREEN_RESULT_DONE = 1,
} ScreenResult;

typedef struct ScreenContext {
	Framebuffer* fb;
	const Input* in;
	const AssetPaths* paths;
	bool allow_input;
	bool audio_enabled;
	bool music_enabled;
} ScreenContext;

struct Screen;

typedef struct ScreenVtable {
	void (*destroy)(struct Screen* self);
	void (*on_enter)(struct Screen* self, const ScreenContext* ctx);
	ScreenResult (*update)(struct Screen* self, const ScreenContext* ctx, double dt_s);
	void (*draw)(struct Screen* self, const ScreenContext* ctx);
	void (*on_exit)(struct Screen* self, const ScreenContext* ctx);
} ScreenVtable;

typedef struct Screen {
	const ScreenVtable* v;
} Screen;
