#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "render/framebuffer.h"

// Simple fullscreen post-processing: a single color wash overlay.
// Intended use: gameplay-only effects (damage flash, poison tint, etc).
// The wash always fades in and fades out.

typedef struct PostFxSystem {
	bool active;
	uint32_t abgr_max; // ABGR8888 (AABBGGRR). Alpha is the peak alpha.
	float fade_in_s;
	float hold_s;
	float fade_out_s;
	float t_s;
} PostFxSystem;

void postfx_init(PostFxSystem* self);
void postfx_reset(PostFxSystem* self);

// Triggers a color wash that fades in, holds, then fades out.
// abgr_max is ABGR8888 (AABBGGRR) where A is peak opacity.
void postfx_trigger_color_wash(PostFxSystem* self, uint32_t abgr_max, float fade_in_s, float hold_s, float fade_out_s);

// Convenience preset: fast red flash for player damage.
void postfx_trigger_damage_flash(PostFxSystem* self);

void postfx_update(PostFxSystem* self, double dt_s);
void postfx_draw(const PostFxSystem* self, Framebuffer* fb);

bool postfx_is_active(const PostFxSystem* self);
