# HUD (Heads-Up Display)

This document explains how Mortum’s in-game HUD (the “Doom-style” bar at the bottom of the screen showing HP, Mortum %, ammo, etc.) is built and rendered today.

## High-level architecture

Mortum’s HUD is an **immediate-mode** overlay drawn directly into the game’s software framebuffer each frame. There is:

- No HUD JSON asset.
- No layout system.
- No retained widget tree.

The HUD is implemented as a single function:

- `hud_draw(...)` in `src/game/hud.c` (declared in `include/game/hud.h`).

It renders:

- A full-width bottom bar with a beveled border.
- Four internal “panels” (HP, Mortum, Ammo (+ optional weapon icon), Keys).
- Optional top-left status text for win/lose states.

## Render order (when and where it draws)

The HUD is drawn during the main game render loop in `src/main.c`.

The important ordering is:

1. 3D world + sprites + particles
2. `weapon_view_draw(...)` (first-person viewmodel)
3. `hud_draw(...)`
4. debug overlay (optional)
5. console overlay (optional)
6. present frame

Why this matters:

- The **weapon viewmodel is intentionally drawn before the HUD** so the HUD can cover the bottom edge of the viewmodel (`include/game/weapon_view.h`).
- Anything drawn after `hud_draw` will appear on top of the HUD.

Implementation note:

- `weapon_view_draw` uses the *same* bar height computation (`fb->height/5` clamped to `[40..80]`) and positions the viewmodel so that its bottom overlaps the bar by a small amount (`overlap_px = 6`). The HUD then draws over it.

## Coordinate system / resolution

The HUD is drawn in **framebuffer pixel coordinates**.

- The framebuffer is created at the engine’s “internal resolution” from config (`render.internal_width`, `render.internal_height` in `config.json`).
- Typical defaults are `640x400`.

All HUD sizing/positioning uses `fb->width` and `fb->height` (internal pixels), so the HUD naturally scales with internal resolution.

## Pixel format and color encoding

### Framebuffer/texture format

While some identifiers say “RGBA”, the actual pixel format used throughout the blitters and image conversion path is:

- **ABGR8888** (A in the most-significant byte, R in the least-significant byte)

This matches:

- `include/render/texture.h`: `Texture.pixels` is “ABGR8888 (matches framebuffer)”
- `include/assets/image.h`: images load to ABGR8888
- `src/assets/image_png.c` / `src/assets/image_bmp.c`: converts surfaces to `SDL_PIXELFORMAT_ABGR8888`

### Literal color constants

HUD panel and bar fills are passed as `uint32_t` literals like `0xFF202020u`.

These should be interpreted as:

- `0xAABBGGRR` (ABGR)

Examples from `src/game/hud.c`:

- `0xFF202020` = opaque dark gray
- `0xFF404040` = highlight bevel
- `0xFF101010` = shadow bevel

### Text colors

Text uses `ColorRGBA` (byte channels) from `include/game/font.h`. The HUD converts from ABGR literals to `ColorRGBA` using a helper:

- `color_from_abgr(0xAABBGGRR)`

The HUD uses these text colors:

- Primary text: `0xFFFFFFFF` (white)
- Mortum text: `0xFFFFE0A0` (warm / amber)
- UNDEAD line: `0xFFFF9090` (pink/red)
- WIN message: `0xFF90FF90` (green)
- LOSE message: `0xFFFF9090` (pink/red)

## HUD layout details (exact math)

The HUD is always anchored to the bottom of the framebuffer.

### Bottom bar

In `src/game/hud.c`:

- `bar_h = fb->height / 5`
- clamped to `[40 .. 80]`
- `bar_y = fb->height - bar_h`

So, with the default internal height of `400`, you get:

- `bar_h = 80`
- `bar_y = 320`

The bar background is a single solid rect fill:

- `draw_rect(fb, 0, bar_y, fb->width, bar_h, bg)`

Then it draws a 2px bevel on all 4 edges (top/left highlight, bottom/right shadow).

### Panels (4 columns)

The bar is subdivided into four panels:

- HP panel
- Mortum panel
- Ammo panel (with optional icon)
- Keys panel

Padding and gap values:

- `pad = 8` (outer inset from the bar)
- `panel_gap = 6` (space between panels)

Panel dimensions:

- `panel_h = bar_h - pad*2`
  - If `panel_h < 20`, it switches to a compact layout:
    - `panel_h = bar_h - 4`
    - `pad = 2`

Panel `y`:

- `panel_y = bar_y + pad`

Panel width:

- `panel_w = (fb->width - pad*2 - panel_gap*3) / 4`
- clamped to `>= 40`

Panel x positions:

- First panel x = `pad`
- Then `x += panel_w + panel_gap` for each subsequent panel

### Panel rendering (background + bevel)

Each panel uses `hud_draw_panel(...)`:

- Fills panel background: `draw_rect(...)`
- Draws a 2px bevel (same approach as the main bar)

Panel background color is currently hardcoded:

- `0xFF282828`

### Text placement

Text is drawn with:

- `font_draw_text(font, fb, x + 6, panel_y + 6, "...", color, 1.0f);`

Important: `font_draw_text` treats the given y as the **top** of a text line and computes the glyph baseline internally:

- `baseline = y_px + ascent*scale`

So the HUD’s `+6` is effectively “top padding”, not baseline padding.

### Special second line in Mortum panel

If:

- `player->undead_active == true` AND
- `panel_h >= 28`

Then the HUD draws a second line at:

- `(x + 6, panel_y + 20)`

This second line is currently the only multi-line behavior inside the bar.

## What the HUD displays (strings and source fields)

All strings are formatted every frame with `snprintf` into a small stack buffer.

### HP panel

String:

- `"HP %d/%d"`

Fields:

- `Player.health`
- `Player.health_max`

Definitions:

- `Player` struct: `include/game/player.h`
- Initialized in `player_init` (`src/game/player.c`)

Updates elsewhere:

- Healing and damage are applied in the main game loop (`src/main.c`).
- Max health upgrades are applied by `upgrades_apply_max_health` (`src/game/upgrades.c`).

### Mortum panel

String:

- `"MORTUM %d%%"`

Field:

- `Player.mortum_pct` (clamped to `[0..100]` by `mortum_add` / `mortum_set` in `src/game/mortum.c`)

Optional second line:

- `"UNDEAD %d/%d"`

Fields:

- `Player.undead_active`
- `Player.undead_shards_collected`
- `Player.undead_shards_required`

Note: In the current codebase, undead fields are reset in `level_start_apply` but do not appear to be set/updated elsewhere yet.

### Ammo panel

String:

- `"AMMO %d/%d"`

Ammo type selection:

- The HUD looks up the currently equipped weapon definition:
  - `weapon_def_get(player->weapon_equipped)`
- Then it reads current/max ammo for that weapon’s `ammo_type`:
  - `ammo_get(&player->ammo, def->ammo_type)`
  - `ammo_get_max(&player->ammo, def->ammo_type)`

Weapon definitions:

- `include/game/weapon_defs.h`, `src/game/weapon_defs.c`

Ammo state:

- `include/game/ammo.h`, `src/game/ammo.c`

Ammo is consumed when firing:

- `weapons_update(...)` calls `ammo_consume(...)` (`src/game/weapons.c`)

Ammo is granted by pickups:

- Player-touch pickup effects are applied in `src/main.c` using `ammo_add(&player.ammo, ...)`.

Ammo max/capacity changes:

- Initial maxes are set in `player_init` (`src/game/player.c`).
- Max ammo upgrades use `ammo_increase_max` via `upgrades_apply_max_ammo` (`src/game/upgrades.c`).

### Weapon icon inside ammo panel (optional)

If `player`, `texreg`, and `paths` are available, the HUD attempts to draw an icon inside the ammo panel.

It uses the current `WeaponId` to look up a `WeaponVisualSpec`:

- `weapon_visual_spec_get(player->weapon_equipped)` (`src/game/weapon_visuals.c`)

Then it builds this filename:

- `Weapons/<dir_name>/<prefix>-ICON.png`

Example (handgun):

- `Weapons/Handgun/HANDGUN-ICON.png`

Loading:

- `texture_registry_get(texreg, paths, filename)` (`src/render/texture.c`)

Where it searches on disk:

- Preferred: `Assets/Images/Textures/<filename>` (64x64 enforced)
- Then: `Assets/Images/Particles/<filename>`
- Then: `Assets/Images/Sprites/<filename>`
- Then: `Assets/Images/Sky/<filename>`
- Fallback: `Assets/Images/<filename>`

For weapon icons, the effective location is:

- `Assets/Images/Weapons/<dir_name>/<prefix>-ICON.png`

Placement:

- Icon is right-aligned inside the ammo panel.
- `dst_x = panel_x + panel_w - icon_width - icon_pad`
- `dst_y = panel_y + (panel_h - icon_height)/2`
- `icon_pad = 6`

Blitting:

- `draw_blit_abgr8888_alpha(...)` (alpha compositing)

Asset convention:

- Weapon art is in `Assets/Images/Weapons/` with per-weapon subdirectories.
- `WeaponVisualSpec.dir_name` must match the folder name.
- `WeaponVisualSpec.prefix` must match the file prefix.

### Keys panel

String:

- `"KEYS %d"`

Field:

- `Player.keys`

The HUD interprets `Player.keys` as a **bitset** and displays:

- `count_bits_u32((unsigned)player->keys)`

Note: In the current entity/pickup system (`include/game/entities.h`), there is no key pickup type yet; `Player.keys` is reset on level start but doesn’t appear to be incremented elsewhere.

### Win/Lose message (top-left)

Independent of the bottom bar, the HUD draws one of these at `(8, 8)`:

- `"YOU ESCAPED"` when `GameState.mode == GAME_MODE_WIN`
- `"YOU DIED"` when `GameState.mode == GAME_MODE_LOSE`

## Font system (how HUD text is rendered)

The HUD uses the shared UI font system.

### Initialization

The UI font is created during startup in `src/main.c`:

- `font_system_init(&ui_font, cfg->ui.font.file, cfg->ui.font.size_px, cfg->ui.font.atlas_size, cfg->ui.font.atlas_size, &paths)`

Config source:

- `config.json` under `ui.font`:
  - `file`: font filename (must be under `Assets/Fonts/`)
  - `size`: pixel size (validated as `size_px`)
  - `atlas_size`: glyph atlas page size

Validation notes:

- `ui.font.file` must be a filename only (no path separators); enforced in `src/core/config.c`.

Operational note:

- The UI font is treated as **startup-only** in the config system (see the config table in `README.md`).

### Font file resolution

The font loader always resolves UI fonts under:

- `Assets/Fonts/<file>`

See `resolve_font_path(...)` in `src/game/font.c`.

### Rendering

`font_draw_text` (`src/game/font.c`) draws text by:

- Packing glyph bitmaps into 1+ atlas pages (8-bit alpha per page).
- For each glyph pixel:
  - multiplies glyph alpha by `ColorRGBA.a`
  - writes a colored ABGR pixel blended over the framebuffer

Important behavior:

- Supports `\n` newlines.
- No rich text / markup.
- Scale is supported (`scale` parameter), but the HUD uses `1.0f`.

## Texture loading / caching (used by the weapon icon)

The HUD relies on `TextureRegistry` (`include/render/texture.h`, `src/render/texture.c`).

Key behaviors:

- First lookup loads from disk and caches the result.
- Failures are also cached (“negative cache”) to avoid logging every frame.
- PNGs are decoded with lodepng and then converted to ABGR8888 via SDL (`src/assets/image_png.c`).

## How to change the HUD safely

### Change text, add/remove panels

Edit `src/game/hud.c`:

- The HUD bar and panels are hard-coded.
- The easiest modifications are:
  - change `snprintf` strings
  - add additional `font_draw_text` calls
  - add additional panels by adjusting `panel_w` / number of columns

Guidelines:

- Keep all math based on `fb->width/height`.
- Avoid heap allocations; the HUD currently allocates nothing per frame.

### Change colors

In `src/game/hud.c`:

- Bar colors: `bg`, `hi`, `lo`
- Panel fill: `0xFF282828`
- Text colors: literals passed through `color_from_abgr(...)`

Reminder: color literals are ABGR (`0xAABBGGRR`).

### Change font

In `config.json`:

- `ui.font.file`: file name in `Assets/Fonts/`
- `ui.font.size`: integer size
- `ui.font.atlas_size`: atlas page size

Then restart (or use the console config reload path if you have one enabled).

### Add/modify weapon icon

- Ensure the icon exists under `Assets/Images/Weapons/<dir>/<prefix>-ICON.png`.
- Ensure `WeaponVisualSpec` for that weapon matches the directory and prefix (`src/game/weapon_visuals.c`).

If the icon does not load:

- `texture_registry_get` will log the paths it tried and then cache the miss.

## HUD change checklist

Use this as a quick, practical checklist when modifying the HUD implementation.

- **Confirm render order**: HUD is drawn after `weapon_view_draw(...)` and before debug/console. If you move calls in `src/main.c`, you can accidentally put the HUD *behind* other overlays or *under* the viewmodel.
- **Mind the viewmodel overlap**: The weapon view intentionally overlaps into the HUD region (`overlap_px = 6` in `src/game/weapon_view.c`). If you change the HUD bar height math or anchoring, also check the viewmodel’s `bar_h/bar_y` math.
- **Use the right color encoding**: HUD fill colors are ABGR literals (`0xAABBGGRR`). Text colors must be converted to `ColorRGBA` (HUD uses `color_from_abgr(...)`). If colors look “swapped”, this is the first place to check.
- **Remember text y semantics**: `font_draw_text(..., x, y, ...)` treats `y` as the *top of the line* and computes baseline internally. If you try to “align to baseline” by intuition, text may drift vertically.
- **Avoid per-frame allocations**: The HUD currently does not allocate per frame. Prefer stack buffers and existing registries/caches.
- **TextureRegistry negative caching gotcha**: A missing icon logs once and then gets cached as a miss. After adding/fixing a file on disk, you may need to restart (or otherwise reset `TextureRegistry`) to see it load.
- **Check internal-resolution impact**: The HUD scales with `render.internal_width/height`. Verify at least one “small” internal height where `bar_h` clamps to 40 and the compact panel layout (`pad = 2`) may trigger.

## Known limitations / current gaps

- HUD layout is not data-driven; everything is hard-coded in `src/game/hud.c`.
- `Player.keys` and undead shard fields are displayed but do not appear to be fully wired up by gameplay systems yet (as of this codebase snapshot).
- Pixel format naming is inconsistent in a few headers (`Framebuffer.pixels` comment says RGBA8888); the runtime path treats pixels as ABGR8888.
