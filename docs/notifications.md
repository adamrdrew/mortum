# Toast Notifications (Developer Documentation)

Mortum includes a small toast notification system for short, gameplay-relevant messages (pickups, status, etc.).

A **toast** is drawn in the **upper-right** and slides in / slides out. Notifications are **queued**: if a toast is already active, new notifications are queued and shown in order.

Source of truth:
- Public API: [include/game/notifications.h](../include/game/notifications.h)
- Implementation: [src/game/notifications.c](../src/game/notifications.c)
- Pickup integration (event application): [src/main.c](../src/main.c)
- Console command integration: [src/game/console_commands.c](../src/game/console_commands.c)

## Goals / Constraints

- **Deterministic**: fixed-capacity FIFO queue, no randomization.
- **Memory-safe**: bounded string copies; no unbounded allocations.
- **Encapsulated**: callers only enqueue notifications; the module owns timing, queueing, and drawing.

## Visual Style

- Toast panel: **gray** background.
- Toast text: **red** using the existing TrueType font system (`FontSystem`).
- Optional icon: drawn on a **black** square background.

## Public API

Declared in [include/game/notifications.h](../include/game/notifications.h).

### Lifetime

- `void notifications_init(Notifications* self)`
  - Zero-initializes the notification manager.

- `void notifications_reset(Notifications* self)`
  - Clears queue and the active toast.
  - Intended to be called on map/world transitions (load/unload).

### Enqueue

- `bool notifications_push_text(Notifications* self, const char* text)`
  - Enqueues a text-only toast.
  - Returns `false` if the queue is full or `text` is invalid.

- `bool notifications_push_icon(Notifications* self, const char* text, const char* icon_filename)`
  - Enqueues a toast with an icon.
  - `icon_filename` is resolved through `TextureRegistry` using the existing asset path search (Sprites/Textures/etc).

### Per-frame

- `void notifications_tick(Notifications* self, float dt_s)`
  - Advances animation and handles dequeueing.

- `void notifications_draw(Notifications* self, Framebuffer* fb, FontSystem* font, TextureRegistry* texreg, const AssetPaths* paths)`
  - Draws the active toast (if any).
  - Safe to call even if some pointers are NULL (it will no-op).

## Behavior Details

### Queueing

- The queue is a fixed-capacity FIFO (`NOTIFICATIONS_QUEUE_CAP`).
- If the queue is full, enqueue returns `false` (the new notification is dropped).

### Timing / Animation (Defaults)

- Slide in: `0.25s`
- Hold: `2.0s`
- Slide out: `0.25s`

These are “sane defaults” and are intended to be easy to tweak in the implementation.

### Long Text: Truncate then Scroll

If the toast text is wider than the available content area:

- The initial view is a **truncated prefix** with an ellipsis (`...`).
- After an initial delay, the display **scrolls horizontally** to reveal more of the text.

Defaults:
- Initial delay before scroll: `0.35s`
- Scroll speed: `95 px/s`
- End pause at end: `0.50s`

Dismissal rule:
- If scrolling is needed, the toast's hold duration is automatically extended so it will not dismiss until it has scrolled to the end (plus the end pause).

Implementation note:
- The toast system does not rely on framebuffer clipping; instead it constructs a substring window that fits the available width.

### Icon Rules (FF00FF Colorkey)

If an icon is provided:

- It is scaled to **24×24** using nearest sampling.
- Pixels with RGB `FF00FF` (magenta) are treated as **transparent** (colorkey), regardless of alpha.
- The icon is drawn over a black background square.

This matches existing sprite conventions used elsewhere in the renderer.

## Pickup Integration: `pickup.notification`

Pickups can optionally emit a toast notification when consumed.

In an entity def (`Assets/Entities/defs/*.json`), inside the `pickup` object:

```json
{
  "pickup": {
    "add_to_inventory": "green_key",
    "notification": "You got the green key",
    "trigger_radius": 0.6,
    "pickup_sound": "Player_Jump.wav",
    "pickup_sound_gain": 1.0
  }
}
```

Notes:
- `notification` is optional.
- The icon is taken from the pickup entity’s `sprite.file.name` and resolved from `Assets/Images/Sprites/` via `TextureRegistry`.

## Console Command: `notify <string>`

A developer command exists to generate a toast at runtime:

- `notify <string>`
  - Example: `notify "Hello there"`

This is useful for testing timing, queueing, and overflow behavior.

## “You Died” Notification

The old HUD-rendered “YOU DIED” text is replaced by a one-shot toast notification on the transition into `GAME_MODE_LOSE` due to player death.

The trigger is edge-detected (fires once per death), not every frame.
