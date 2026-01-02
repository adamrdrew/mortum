#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "render/framebuffer.h"

// Simple fullscreen post-processing: blended color wash overlays.
// Intended use: gameplay-only effects (damage flash, poison tint, etc).
// Each overlay always fades in and fades out.

#define POSTFX_MAX_EFFECTS 4

typedef enum PostFxTag {
	POSTFX_TAG_NONE = 0,
	POSTFX_TAG_STATUS_TINT = 1,
	POSTFX_TAG_DAMAGE_FLASH = 2,
} PostFxTag;

typedef struct PostFxEffect {
	bool active;
	PostFxTag tag;
	int priority; // Higher draws later (on top).
	uint32_t serial; // Monotonic trigger counter for stable ordering.
	uint32_t abgr_max; // ABGR8888 (AABBGGRR). Alpha is the peak alpha.
	float fade_in_s;
	float hold_s;
	float fade_out_s;
	float t_s;
} PostFxEffect;

typedef struct PostFxSystem {
	PostFxEffect effects[POSTFX_MAX_EFFECTS];
	uint32_t next_serial;
} PostFxSystem;

void postfx_init(PostFxSystem* self);
void postfx_reset(PostFxSystem* self);

// Triggers a color wash that fades in, holds, then fades out.
// abgr_max is ABGR8888 (AABBGGRR) where A is peak opacity.
void postfx_trigger_color_wash(PostFxSystem* self, uint32_t abgr_max, float fade_in_s, float hold_s, float fade_out_s);

// Triggers (or replaces) a tagged wash.
// Use tags to avoid unbounded stacking for long-lived effects (e.g., status tints)
// and to ensure short flashes (e.g., damage) restart cleanly.
// If the pool is full, the new effect is dropped unless it can evict a lower-priority one.
void postfx_trigger_tagged_color_wash(
	PostFxSystem* self,
	PostFxTag tag,
	int priority,
	uint32_t abgr_max,
	float fade_in_s,
	float hold_s,
	float fade_out_s);

void postfx_clear_tag(PostFxSystem* self, PostFxTag tag);

// Convenience preset: fast red flash for player damage.
void postfx_trigger_damage_flash(PostFxSystem* self);

void postfx_update(PostFxSystem* self, double dt_s);
void postfx_draw(const PostFxSystem* self, Framebuffer* fb);

bool postfx_is_active(const PostFxSystem* self);
