# Text Rendering (TrueType / stb_truetype)

Mortum renders all on-screen text via a small TrueType font system backed by `stb_truetype`.

This document is written for developers (including agent workflows) who need to draw text in the engine.

## TL;DR

- Public API: [include/game/font.h](../include/game/font.h)
- Implementation: [src/game/font.c](../src/game/font.c)
- Font is loaded once at startup from config, then passed to UI draw calls.
- Draw text with:
  - `font_draw_text(font, fb, x, y, "Hello", color_rgba(255,255,255,255), 1.0f);`

## Configuration

Text uses the UI font config:

```json
"ui": {
  "font": {
    "file": "ProggyClean.ttf",
    "size": 15,
    "atlas_size": 512
  }
}
```

Rules:

- `ui.font.file` is a **filename only** and is always loaded from `Assets/Fonts/`.
  - Example: `"ProggyClean.ttf"`
  - Paths like `"Assets/Fonts/..."` or `"../"` are rejected by config validation.
- `ui.font.size` is the font pixel height (int, validated in `[6..96]`).
- `ui.font.atlas_size` sets a **square** glyph atlas page size (int, validated in `[128..4096]`).

Reload semantics:

- UI font settings are **startup-only**. Runtime config reload does not rebuild the font system.

## Runtime ownership and initialization

The UI font system is created in the main startup path and is expected to live until shutdown.

- Creation: `font_system_init(&ui_font, cfg->ui.font.file, cfg->ui.font.size_px, cfg->ui.font.atlas_size, cfg->ui.font.atlas_size, &paths)`
- Destruction: `font_system_shutdown(&ui_font)`

The engine passes a `FontSystem*` down to UI rendering code (HUD, overlays). If you are adding a new UI module, follow the same pattern:

- Add a `FontSystem* font` parameter to your draw function.
- Pass the pointer from the caller that already owns the UI font (currently the main loop).

## Drawing text

### Core call

```c
#include "game/font.h"

ColorRGBA white = color_rgba(255, 255, 255, 255);
font_draw_text(font, fb, 16, 16, "Hello", white, 1.0f);
```

Parameters:

- `font`: `FontSystem*` (must be initialized).
- `fb`: destination `Framebuffer*`.
- `x_px`, `y_px`: top-left starting position, in framebuffer pixels.
- `text`: UTF-8 bytes are accepted, but the cache is currently **byte-based** (see Limitations).
- `color`: `ColorRGBA` (r,g,b,a).
- `scale`: additional multiplier over the configured size. Use `1.0f` for normal text.

Text background is always transparent; only glyph alpha is blended.

### Newlines

`\n` is supported. Each newline moves to the next baseline using `font_line_height(font, scale)`.

### Color conversion helper

Many existing code paths use packed `0xAABBGGRR` (ABGR) values.

If you have an ABGR color constant and need a `ColorRGBA`, convert it like this:

```c
static inline ColorRGBA color_from_abgr(uint32_t abgr) {
    return (ColorRGBA){
        .r = (uint8_t)(abgr & 0xFFu),
        .g = (uint8_t)((abgr >> 8) & 0xFFu),
        .b = (uint8_t)((abgr >> 16) & 0xFFu),
        .a = (uint8_t)((abgr >> 24) & 0xFFu),
    };
}
```

## Measuring text

Use these helpers for layout:

- `font_measure_text_width(font, text, scale)`
  - Returns the max line width across newlines.
- `font_line_height(font, scale)`
  - Returns the line height in pixels.

Typical right-aligned drawing:

```c
int w = font_measure_text_width(font, label, 1.0f);
int x = fb->width - 8 - w;
font_draw_text(font, fb, x, 8, label, color_rgba(255,255,255,255), 1.0f);
```

## Glyph caching and atlas behavior

The font system rasterizes glyphs on demand using `stb_truetype` and caches results in an atlas.

- Cache keying: currently `glyphs[256]` keyed by **byte value**.
- Atlas pages: a simple deterministic shelf packer.
- When an atlas page fills, the system allocates a new page (same dimensions).

Practical implications:

- ASCII HUD/debug text is fast after warm-up.
- Non-ASCII is not handled robustly yet (see Limitations).

## Smoke test page (debug)

There is a small built-in smoke test to validate rendering:

- Toggle via the in-game console command: `show_font_test true|false`.
- When enabled, the engine calls `font_draw_test_page(font, fb, 16, 16)`.

This page renders:

- ASCII samples
- Multiple lines
- Several scale factors
- Basic color tests
- Current stats (cached glyph count, page count, atlas size)

## Where to change things

- Font rendering implementation: [src/game/font.c](../src/game/font.c)
- Public API/types: [include/game/font.h](../include/game/font.h)
- Config schema + parsing: [include/core/config.h](../include/core/config.h), [src/core/config.c](../src/core/config.c)
- Default config file: [config.json](../config.json)
- Runtime toggle + integration: [src/main.c](../src/main.c)

## Limitations / known constraints

- Character set: glyph cache is currently **byte-based** (`0..255`). This is sufficient for ASCII UI text but not true Unicode text rendering.
- Kerning: kerning is applied using `stbtt_GetCodepointKernAdvance` for adjacent bytes; this is reasonable for ASCII, but not for full UTF-8.
- Scaling quality: scale factors other than `1.0` use nearest-neighbor sampling of the cached alpha.

If you need full Unicode/UTF-8 support, the next step is decoding UTF-8 to codepoints and changing the glyph cache key to a codepoint map (and possibly expanding atlas management).
