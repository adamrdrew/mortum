#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "assets/asset_paths.h"
#include "game/font.h"
#include "render/framebuffer.h"
#include "render/texture.h"

// Toast notifications: upper-right slide-in/slide-out messages with optional icon.
//
// Design goals:
// - Deterministic: fixed-capacity queue; no dynamic allocations.
// - Memory-safe: bounded string copies; graceful handling of NULL pointers.
// - Clean API: push text-only or text+icon, then tick/draw each frame.

#define NOTIFICATIONS_QUEUE_CAP 16
#define NOTIFICATION_TEXT_MAX 256
#define NOTIFICATION_ICON_NAME_MAX 128

typedef enum NotificationPhase {
	NOTIFY_PHASE_IN = 0,
	NOTIFY_PHASE_HOLD = 1,
	NOTIFY_PHASE_OUT = 2,
} NotificationPhase;

typedef struct NotificationItem {
	char text[NOTIFICATION_TEXT_MAX];
	bool has_icon;
	char icon_filename[NOTIFICATION_ICON_NAME_MAX]; // filename relative to Images/* dirs (e.g. "green_key.png")
} NotificationItem;

typedef struct Notifications {
	// FIFO queue (deterministic).
	NotificationItem queue[NOTIFICATIONS_QUEUE_CAP];
	uint32_t head;
	uint32_t count;

	// Currently displayed toast.
	bool active;
	NotificationItem cur;
	NotificationPhase phase;
	float phase_t_s;

	// Text overflow scrolling (computed deterministically from time).
	float hold_t_s;
	float hold_target_s;
} Notifications;

void notifications_init(Notifications* self);
void notifications_reset(Notifications* self);

// Enqueue a text-only toast. Returns false if the queue is full or text is invalid.
bool notifications_push_text(Notifications* self, const char* text);

// Enqueue a toast with an icon filename resolved via TextureRegistry (Sprites/Textures/etc).
bool notifications_push_icon(Notifications* self, const char* text, const char* icon_filename);

void notifications_tick(Notifications* self, float dt_s);

// Draws the active toast (if any). Safe to call even if inputs are NULL.
void notifications_draw(
	Notifications* self,
	Framebuffer* fb,
	FontSystem* font,
	TextureRegistry* texreg,
	const AssetPaths* paths);
