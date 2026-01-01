# HUD (Heads-Up Display)

Mortum’s HUD is a **constrained, data-driven, immediate-mode** overlay drawn directly into the software framebuffer each frame.

The HUD is intentionally *not* a general UI layout system:

- Layout is always a single bottom bar with a **single row of equal-width widget panels**.
- Only **styling** (colors/images/bevel/shadow/text) and **widget order** come from JSON.
- No per-frame JSON parsing, allocations, or disk I/O.

Implementation locations:

- Runtime + renderer: `HudSystem` ([include/game/hud.h](../include/game/hud.h), [src/game/hud.c](../src/game/hud.c))
- JSON schema + loader: `HudAsset` / `hud_asset_load(...)` ([include/assets/hud_loader.h](../include/assets/hud_loader.h), [src/assets/hud_loader.c](../src/assets/hud_loader.c))
- Default asset: [Assets/HUD/default.json](../Assets/HUD/default.json)

## Configuration and reload semantics

- Config key: `ui.hud.file`
- Value: filename under `Assets/HUD/` (no path separators), e.g. `default.json`

Validation behavior:

- Startup: invalid HUD asset is fatal (startup fails).
- Reload: invalid HUD asset prevents config commit and keeps the previous config/HUD.
- Console `config_reload`: after a successful config reload, the engine attempts `hud_system_reload(...)`; on failure it logs a warning and keeps the previous HUD.

## Render order and the weapon overlap invariant

The HUD is drawn in the main render loop after the first-person weapon viewmodel:

1. 3D world + sprites + particles
2. `weapon_view_draw(...)`
3. `hud_draw(...)` (draws the HUD bar + widgets)
4. debug overlay (optional)
5. console overlay (optional)
6. present frame

Why this matters:

- The viewmodel is positioned so its bottom overlaps the HUD area slightly; the HUD must draw after it to cover that overlap.
- Anything drawn after `hud_draw(...)` will appear on top of the HUD.

## Coordinate system and sizing

The HUD draws in **framebuffer pixel coordinates** (`fb->width`, `fb->height`). The framebuffer is created at the engine “internal resolution” (`render.internal_width`, `render.internal_height`).

### Bar height (classic)

The HUD bar height preserves the classic behavior:

$$
\text{bar\_h} = \mathrm{clamp}(\lfloor fb->height / 5 \rfloor, 40, 80)
$$

and is anchored at the bottom:

- `bar_y = fb->height - bar_h`

## Styling model (what the JSON controls)

The HUD asset controls:

- Bar styling: background (color or scaled image) + bevel
- Panel styling: background (color or scaled image) + bevel + shadow
- Text styling: font override (optional), colors, padding, and deterministic fit settings
- Widget order: which widgets appear, left-to-right

### Backgrounds

Both the bar and the widget panels support:

- `mode: "color"` + `color_abgr`
- `mode: "image"` + `image` (a safe relative path resolved via the normal texture search rules)

Images are drawn scaled to the destination rect using nearest-neighbor and alpha blending.

### Bevel and shadow

- Bevel draws a simple highlight/shadow border with configurable thickness.
- Shadow draws a tinted rectangle behind the panel with configurable offset.

### Text fitting (deterministic)

Each widget string is fit to available width deterministically:

1. Try scale values from `max_scale` down to `min_scale` in fixed steps of `0.05`.
2. If it still doesn’t fit at `min_scale`, truncate and append `...`.

## Widgets

The JSON chooses the ordering of these widget kinds:

- `health`: `HP <cur>/<max>`
- `mortum`: `MORTUM <pct>%` (and, when active and there is vertical room, a second line: `UNDEAD <collected>/<required>`)
- `ammo`: `AMMO <cur>/<max>` (may draw a weapon icon on the right)
- `equipped_weapon`: `WEAPON <name>`
- `keys`: `KEYS <count>` (count of set bits in the player key bitset)

### Ammo widget weapon icon

If textures are available, the ammo widget attempts to draw an icon derived from the current weapon’s visual spec:

- File pattern: `Weapons/<dir_name>/<prefix>-ICON.png`

The lookup uses the texture registry (which caches results); it should not cause per-frame disk activity in steady state.

## HUD JSON schema

HUD assets live under `Assets/HUD/` and must match the schema enforced by `hud_asset_load(...)`.

Root:

- `version` (required): `1`
- `bar` (required): bar styling and layout parameters
- `widgets` (required): panel styling and widget ordering

`bar`:

- `height_mode`: must be `"classic"`
- `padding_px`: int in `[0..64]`
- `gap_px`: int in `[0..64]`
- `background`:
  - `mode`: `"color" | "image"`
  - `color_abgr`: uint32 encoded as a JSON number (ABGR)
  - `image`: string; required when `mode="image"`; must be a safe relative path
- `bevel`:
  - `enabled`: bool
  - `hi_abgr`: uint32 ABGR
  - `lo_abgr`: uint32 ABGR
  - `thickness_px`: int in `[0..8]`

`widgets.panel`:

- `background`: same schema as `bar.background`
- `bevel`: same schema as `bar.bevel`
- `shadow`:
  - `enabled`: bool
  - `offset_x`: int in `[-32..32]`
  - `offset_y`: int in `[-32..32]`
  - `color_abgr`: uint32 ABGR
- `text`:
  - `font_file`: optional; if present must be filename-only under `Assets/Fonts/`
  - `color_abgr`: uint32 ABGR
  - `accent_color_abgr`: uint32 ABGR
  - `padding_x`: int in `[0..64]`
  - `padding_y`: int in `[0..64]`
  - `fit`:
    - `min_scale`: float in `[0.1..1.0]`
    - `max_scale`: float in `[0.1..2.0]` and must satisfy `max_scale >= min_scale`

`widgets.order`:

- Array of objects with:
  - `kind`: one of `health`, `mortum`, `ammo`, `equipped_weapon`, `keys`
- Must contain at least 1 entry.
- Maximum entries: `HUD_MAX_WIDGETS` (currently 8); extras are ignored with a warning.

## Example asset

See the full default asset at [Assets/HUD/default.json](../Assets/HUD/default.json). A minimal valid asset looks like:

```json
{
  "version": 1,
  "bar": {
    "height_mode": "classic",
    "padding_px": 8,
    "gap_px": 6,
    "background": { "mode": "color", "color_abgr": 4280295456, "image": "" },
    "bevel": { "enabled": true, "hi_abgr": 4282400832, "lo_abgr": 4279242768, "thickness_px": 2 }
  },
  "widgets": {
    "panel": {
      "background": { "mode": "color", "color_abgr": 4280821800, "image": "" },
      "bevel": { "enabled": true, "hi_abgr": 4282400832, "lo_abgr": 4279242768, "thickness_px": 2 },
      "shadow": { "enabled": true, "offset_x": 1, "offset_y": 1, "color_abgr": 2147483648 },
      "text": {
        "font_file": "",
        "color_abgr": 4294967295,
        "accent_color_abgr": 4294957472,
        "padding_x": 6,
        "padding_y": 6,
        "fit": { "min_scale": 0.65, "max_scale": 1.0 }
      }
    },
    "order": [
      { "kind": "health" },
      { "kind": "mortum" },
      { "kind": "ammo" },
      { "kind": "equipped_weapon" }
    ]
  }
}
```
# HUD (Heads-Up Display)

Mortum’s HUD is a **constrained, data-driven, immediate-mode** overlay drawn directly into the game’s software framebuffer each frame.

Key constraints:

- Single row of **equal-width** widget panels.
- Styling and widget ordering are loaded from a HUD JSON asset at startup/reload (no per-frame parsing).
- No retained widget tree.

Implementation locations:

- Runtime + renderer: `HudSystem` (`include/game/hud.h`, `src/game/hud.c`)
- JSON schema + loader: `HudAsset` / `hud_asset_load(...)` (`include/assets/hud_loader.h`, `src/assets/hud_loader.c`)
- Default asset: `Assets/HUD/default.json`

Configuration:

- `ui.hud.file` selects a HUD asset under `Assets/HUD/`.

Note: parts of this document below still describe legacy behavior and should be treated as historical context.

## Render order (when and where it draws)

The HUD is drawn during the main game render loop in `src/main.c`.

The important ordering is:

1. 3D world + sprites + particles
2. `weapon_view_draw(...)` (first-person viewmodel)
3. `hud_draw(...)` (using the `HudSystem`)
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

3. `hud_draw(...)` (using the `HudSystem`)

This second line is currently the only multi-line behavior inside the bar.

## What the HUD displays (strings and source fields)

All strings are formatted every frame with `snprintf` into a small stack buffer.

### HP panel

String:

- `"HP %d/%d"`

Fields:

- `Player.health`
- `Player.health_max`
- `weapon_view_draw` uses the *same* bar height computation (`fb->height/5` clamped to `[40..80]`) and positions the viewmodel so that its bottom overlaps the bar by a small amount (`overlap_px = 6`). The HUD then draws over it.

## Pixel format and color encoding

## HUD layout details
Definitions:

- `Player` struct: `include/game/player.h`
- Initialized in `player_init` (`src/game/player.c`)

Updates elsewhere:

- `bar_h = fb->height / 5`
- clamped to `[40 .. 80]`
- `bar_y = fb->height - bar_h`
### Mortum panel

String:
- `bar_h = 80`
- `bar_y = 320`

Field:

- `draw_rect(fb, 0, bar_y, fb->width, bar_h, bg)`

Optional second line:

- `"UNDEAD %d/%d"`

Fields:

- HP panel
- Mortum panel
- Ammo panel (with optional icon)
- Keys panel
Note: In the current codebase, undead fields are reset in `level_start_apply` but do not appear to be set/updated elsewhere yet.

### Ammo panel
- `pad = 8` (outer inset from the bar)
- `panel_gap = 6` (space between panels)

- `"AMMO %d/%d"`

- `panel_h = bar_h - pad*2`
  - If `panel_h < 20`, it switches to a compact layout:
    - `panel_h = bar_h - 4`
    - `pad = 2`
- Then it reads current/max ammo for that weapon’s `ammo_type`:
  - `ammo_get(&player->ammo, def->ammo_type)`
  - `ammo_get_max(&player->ammo, def->ammo_type)`
- `panel_y = bar_y + pad`
Weapon definitions:

- `include/game/weapon_defs.h`, `src/game/weapon_defs.c`
- `panel_w = (fb->width - pad*2 - panel_gap*3) / 4`
- clamped to `>= 40`

- `include/game/ammo.h`, `src/game/ammo.c`

- First panel x = `pad`
- Then `x += panel_w + panel_gap` for each subsequent panel
- `weapons_update(...)` calls `ammo_consume(...)` (`src/game/weapons.c`)

Ammo is granted by pickups:

- Player-touch pickup effects are applied in `src/main.c` using `ammo_add(&player.ammo, ...)`.
- Fills panel background: `draw_rect(...)`
- Draws a 2px bevel (same approach as the main bar)

- Initial maxes are set in `player_init` (`src/game/player.c`).
- Max ammo upgrades use `ammo_increase_max` via `upgrades_apply_max_ammo` (`src/game/upgrades.c`).
- `0xFF282828`
### Weapon icon inside ammo panel (optional)

If `player`, `texreg`, and `paths` are available, the HUD attempts to draw an icon inside the ammo panel.

It uses the current `WeaponId` to look up a `WeaponVisualSpec`:
- `font_draw_text(font, fb, x + 6, panel_y + 6, "...", color, 1.0f);`
- `weapon_visual_spec_get(player->weapon_equipped)` (`src/game/weapon_visuals.c`)

Then it builds this filename:
- `baseline = y_px + ascent*scale`
- `Weapons/<dir_name>/<prefix>-ICON.png`

Example (handgun):

- `Weapons/Handgun/HANDGUN-ICON.png`

Loading:
- `player->undead_active == true` AND
- `panel_h >= 28`

Where it searches on disk:

- `(x + 6, panel_y + 20)`
- Then: `Assets/Images/Particles/<filename>`
- Then: `Assets/Images/Sprites/<filename>`
- Then: `Assets/Images/Sky/<filename>`
- Fallback: `Assets/Images/<filename>`

For weapon icons, the effective location is:

- `health`: `HP <cur>/<max>`
- `mortum`: `MORTUM <pct>%` (and, when active, an `UNDEAD <collected>/<required>` second line)
- `ammo`: `AMMO <cur>/<max>` (may draw a weapon icon inside the panel)
- `equipped_weapon`: `WEAPON <name>`
- `keys`: `KEYS <count>`
- `dst_x = panel_x + panel_w - icon_width - icon_pad`
- `dst_y = panel_y + (panel_h - icon_height)/2`
- `icon_pad = 6`

Blitting:
- `ui.hud.file` (filename under `Assets/HUD/`, e.g. `default.json`)
- `draw_blit_abgr8888_alpha(...)` (alpha compositing)

Asset convention:
- Startup: invalid HUD asset is fatal (startup aborts).
- `config_reload`: invalid HUD asset keeps the previous config (and therefore keeps the previous HUD).
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
