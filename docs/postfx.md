# Post-FX (Gameplay-Only Fullscreen Color Wash)

This document describes Mortum’s **post-processing** (Post-FX) system as implemented in C.

Scope and design intent:

- **Gameplay-only:** affects the 3D world + sprites + weapon view, but **does not** affect HUD, debug overlays, or menus/screens.
- **Simple effects:** currently **fullscreen color wash overlays** (tinted overlays).
- **Blending:** the system supports **multiple active overlays** at once (e.g. status tint + damage flash).
- **Timing:** every effect **always fades in and fades out** (optionally with a hold).

Current implementation:

- Public API: [include/game/postfx.h](../include/game/postfx.h)
- Runtime implementation: [src/game/postfx.c](../src/game/postfx.c)
- Integration point (render order + event trigger): [src/main.c](../src/main.c)


## 1) Mental model

Think of Post-FX as a tiny set of state machines that, each frame:

1. Advances each effect’s internal timer (`t_s`) in `postfx_update(...)`.
2. Computes each effect’s current intensity scalar `k` in `postfx_draw(...)` based on fade-in/hold/fade-out.
3. Draws each active overlay as a fullscreen alpha-blended rectangle over the framebuffer.

The key property is *where* that rectangle is drawn:

- It is drawn **after** gameplay rendering is complete (world/sprites/particles + viewmodel).
- It is drawn **before** the HUD and other UI overlays.

This ensures:

- The overlay tints the gameplay.
- The overlay does **not** tint the HUD, menus, or screens.


## 2) Render-order contract

### 2.1 Gameplay branch

In the gameplay render path, Post-FX is drawn between the weapon viewmodel and the HUD:

1. 3D world + sprites + particles
2. `weapon_view_draw(...)`
3. `postfx_draw(...)`  ← Post-FX overlay
4. `hud_draw(...)`     ← HUD is not tinted
5. debug overlay (optional)
6. console overlay
7. present

This ordering is enforced in [src/main.c](../src/main.c).

### 2.2 Screens/menus branch

When a `Screen` is active (`screen_runtime_is_active(...)`), the engine uses the screen runtime’s own draw routine (`screen_runtime_draw(...)`). Post-FX is **not** drawn in this path.

Additionally, `postfx_reset(...)` is called when `screen_active` is true so that gameplay effects do not “leak” into menus/scenes if a screen is opened immediately after damage.


## 3) Public API overview

Declared in [include/game/postfx.h](../include/game/postfx.h).

### 3.1 Types

`PostFxSystem` is intentionally small and plain:

Internally it holds a small fixed pool (`POSTFX_MAX_EFFECTS`) of `PostFxEffect` slots.

Each `PostFxEffect` stores:

- `active`: whether that slot is running.
- `tag`: optional semantic identifier (damage flash, status tint, etc.).
- `priority`: higher numbers draw later (on top).
- `serial`: monotonic trigger counter for stable ordering.
- `abgr_max`: the overlay’s color at peak intensity.
- `fade_in_s`, `hold_s`, `fade_out_s`: effect timing parameters (seconds).
- `t_s`: elapsed time since trigger.

### 3.2 Lifecycle

- `postfx_init(PostFxSystem*)`
  - Zero-initializes the system.
- `postfx_reset(PostFxSystem*)`
  - Clears the current effect and timer.
- `postfx_update(PostFxSystem*, double dt_s)`
  - Advances the timer and automatically deactivates when the effect completes.
- `postfx_draw(const PostFxSystem*, Framebuffer*)`
  - Draws the current overlay (if active) into the framebuffer.

### 3.3 Triggering effects

Two entry points exist:

- `postfx_trigger_color_wash(PostFxSystem*, uint32_t abgr_max, float fade_in_s, float hold_s, float fade_out_s)`
  - General-purpose trigger. Uses `POSTFX_TAG_NONE` and priority 0.
- `postfx_trigger_damage_flash(PostFxSystem*)`
  - Convenience preset used by gameplay when the player takes damage.

For effects that should not stack unboundedly (like a long status tint, or a damage flash that can retrigger frequently), use tagged triggers:

- `postfx_trigger_tagged_color_wash(...)`
  - If a slot with the same tag is already active, it is replaced/restarted.
  - If the pool is full, the new effect is dropped unless it can evict a lower-priority effect.
- `postfx_clear_tag(...)`
  - Stops any active effects with that tag.


## 4) Color format and alpha blending

### 4.1 ABGR8888

The overlay color is expressed as a 32-bit **ABGR8888** literal:

- Layout: `0xAABBGGRR`
- The high byte (`AA`) is **alpha** (opacity).

Example:

- `0xD00000FF` means:
  - `AA = 0xD0` (high opacity)
  - `BB = 0x00`
  - `GG = 0x00`
  - `RR = 0xFF` (full red)

### 4.2 How it’s drawn

`postfx_draw(...)` calls:

- `draw_rect_abgr8888_alpha(fb, 0, 0, fb->width, fb->height, abgr)`

This alpha-blends the overlay into the framebuffer.


## 5) Timing model (fade-in / hold / fade-out)

### 5.1 Phases

An effect’s total duration is:

- `total = fade_in_s + hold_s + fade_out_s`

The system computes an intensity scalar `k` in `[0..1]`:

- Fade-in phase (`t < fade_in_s`):
  - $k = t / fade\_in\_s$
- Hold phase (`fade_in_s <= t < fade_in_s + hold_s`):
  - $k = 1$
- Fade-out phase (remaining time):
  - $k = 1 - (t_{out} / fade\_out\_s)$

Then it computes the actual alpha used this frame:

- `a = round(a_max * k)`

If `a == 0`, the draw is skipped.

### 5.2 “Always fades in/out” guarantee

`postfx_trigger_color_wash(...)` enforces non-zero fade times by clamping `fade_in_s` and `fade_out_s` to a tiny epsilon (`min_fade`). This prevents divide-by-zero and keeps the design contract: **no instant-on / instant-off effects**.

### 5.3 dt clamping

`postfx_update(...)` clamps `dt_s` to a maximum (0.25s). This matches patterns elsewhere in the engine that avoid large time steps from producing extreme jumps.


## 6) How it is integrated today

### 6.1 Initialization

The engine constructs one `PostFxSystem` instance in `main`:

- `PostFxSystem postfx;`
- `postfx_init(&postfx);`

### 6.2 Update

In the **gameplay** branch of the main loop, once per frame:

- `postfx_update(&postfx, frame_dt_s);`

### 6.3 Draw

In the **gameplay render** path:

- `weapon_view_draw(...)`
- `postfx_draw(&postfx, &fb);`
- `hud_draw(...)`

### 6.4 Preventing screen leakage

When a `Screen` is active, Post-FX is cleared:

- `postfx_reset(&postfx);`

This ensures menus/scenes are never tinted.

### 6.5 Trigger: player damage

When the player is damaged (see `ENTITY_EVENT_PLAYER_DAMAGE` handling), gameplay calls:

- `postfx_trigger_damage_flash(&postfx);`


## 7) Extension guidelines

The system is intentionally minimal; extending it should preserve the render-order contract and keep the API clean.

### 7.1 Adding new “named effects”

Recommended pattern:

1. Add a new convenience function in [include/game/postfx.h](../include/game/postfx.h), e.g.:

   - `void postfx_trigger_poison_tint(PostFxSystem* self, float seconds);`

2. Implement it in [src/game/postfx.c](../src/game/postfx.c) by calling `postfx_trigger_color_wash(...)` with a tuned color and timings.

This keeps call sites (gameplay systems) clean and avoids duplicating magic numbers throughout the codebase.

### 7.2 Making effects dynamic (duration, strength)

Use the general trigger API for dynamic effects:

- `postfx_trigger_color_wash(&postfx, abgr, fade_in, hold, fade_out);`

Where “dynamic” typically means:

- Hold time depends on the status effect duration.
- Alpha depends on severity.
- Fade-out might be shortened if the effect ends early.

If you find yourself adding many ad-hoc parameters in call sites, prefer adding a small helper function (a named effect trigger) that takes semantic parameters (e.g., `severity`, `duration_s`).

### 7.3 Blending + stacking policy (current behavior)

Current behavior is **multi-slot**:

- Multiple effects can be active at the same time.
- Effects are blended by drawing fullscreen alpha rectangles sequentially.
- Draw order is stable:
  - lower `priority` first (under)
  - higher `priority` later (over)
  - for equal priority, older (`serial` smaller) draws first

In practice:

- A low-priority status tint can be active continuously.
- A high-priority damage flash can play on top whenever triggered.

If you later want stacking, there are two common approaches:

1. **Priority override:** keep one active effect, but allow higher-priority effects (damage) to interrupt lower-priority ones (poison).
2. **Layered blending:** maintain a small fixed array of active effects and blend them (e.g., max alpha, additive, or lerp).

If you implement stacking, you must decide:

- How to combine colors.
- Whether multiple effects can share the same fade curves.
- How to bound cost (fixed-size array; no per-frame allocation).

### 7.4 Where to hook new triggers

Common integration points:

- Player state changes:
  - damage → red flash
  - healing → green/blue flash
  - poison → persistent green/purple tint
- Entity events:
  - player touch pickups
  - status effects applied/removed

Make sure triggers run only during gameplay (not while a `Screen` is active). The simplest safe approach is to keep triggering in gameplay-only code paths.


## 8) Tuning tips

### 8.1 Make it visible without being annoying

Two knobs matter most:

- **Peak alpha** (`AA` in `0xAABBGGRR`)
- **Time** (fade/hold/out)

Rules of thumb:

- If it feels like “a single-frame pop,” increase `hold_s` slightly (even 0.02–0.05s helps).
- If it’s visible but too subtle, increase alpha (e.g., `0xA0...` → `0xD0...`).
- If it overstays its welcome, shorten `fade_out_s`.

### 8.2 Damage flash preset

The current preset is defined in `postfx_trigger_damage_flash(...)`.

It is intended to be:

- very fast
- unmistakable
- not long enough to obscure aiming


## 9) Pitfalls and invariants

- **Do not draw Post-FX after `hud_draw(...)`** unless you explicitly want the HUD tinted.
- **Do not draw Post-FX in the screen runtime branch** (menus/scenes), unless a particular screen effect is desired. Screens already implement their own fades (see `scene_screen.c`).
- **Be careful with color byte order.** This code uses ABGR8888 (AABBGGRR), not RGBA.
- **Avoid allocations** in per-frame paths; this system is allocation-free.


## 10) Quick examples

### 10.1 Trigger a healing flash (example)

```c
// Bright green, quick in/out, tiny hold
postfx_trigger_color_wash(&postfx, 0xA000FF00u, 0.02f, 0.03f, 0.08f);
```

### 10.2 Trigger a poison tint (example)

```c
// Purple tint with slow fades and a long hold
postfx_trigger_color_wash(&postfx, 0x6000A0FFu, 0.20f, 2.0f, 0.30f);
```

(These are examples; choose colors and timings that match gameplay.)
