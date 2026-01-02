# Scene System (Standalone Screen)

This document is the source of truth for Mortum’s **Scene** feature and the minimal internal **Screen** runtime that powers it.

Scenes are **developer-facing** content used for debugging/testing and future UI flows. Scenes can run standalone (console / `--scene`) and can also be run by Timelines (via TimelineFlow events). While a Scene is active, normal gameplay update/render is suspended.

## Overview

- **Scene assets** are JSON files under `Assets/Scenes/`.
- A Scene runs as a single active **Screen** until it completes.
- Scenes can be started via:
  - in-game console: `load_scene <scene_file>`
  - CLI: `--scene <scene_file>` (runs the scene and exits when it completes)

## File/path conventions

All paths are resolved relative to the asset root using `AssetPaths`.

- Scene JSON: `Assets/Scenes/<scene_file>`
- Background image: `Assets/Images/<background_png>`
- Music MIDI: `Assets/Sounds/MIDI/<midi>`
- SoundFont: `Assets/Sounds/SoundFonts/<soundfont>`
- Scene SFX (WAV): `Assets/Sounds/Effects/<enter_wav|exit_wav>`
- Fonts (TTF): `Assets/Fonts/<font>`

### Safe relative path rules

For Scene JSON and all referenced asset paths/filenames:

- Must be **relative** (cannot start with `/` or `\\`)
- Must not contain `..`
- Must not contain `\\`
- Allowed characters: `[A-Za-z0-9_./-]`

This permits subfolders like `Scenes/intro/title.png` while still preventing traversal.

## JSON schema

Root object fields:

### Required

- `background_png` (string)
  - Path relative to `Assets/Images/`
  - Must end with `.png`

- `end` (object)
  - End-condition configuration (see “End conditions”)

### Optional

- `music` (object)
  - `midi` (string): filename relative to `Assets/Sounds/MIDI/` (must end with `.mid` or `.midi`)
  - `soundfont` (string): filename relative to `Assets/Sounds/SoundFonts/` (must end with `.sf2`)
  - If `midi` is present and `soundfont` is omitted, it defaults to `hl4mgm.sf2`.
  - `no_stop` (bool): if true (and `midi` is not provided), the Scene will **not** stop any currently playing MIDI background music.

    Note: if both `midi` and `no_stop` are set, the engine logs a warning and ignores `no_stop`.

- `sfx` (object)
  - `enter_wav` (string): filename relative to `Assets/Sounds/Effects/` (must end with `.wav`)
  - `exit_wav` (string): filename relative to `Assets/Sounds/Effects/` (must end with `.wav`)

- `text` (object)
  - `value` (string): inline UTF-8 text
  - `align` (string): `left | center | right` (default `center`)
  - `font` (string): TTF filename in `Assets/Fonts/` (default `ProggyClean.ttf`)
  - `size_px` (int): font pixel height (default `16`, validated `[6..96]`)
  - `atlas_size` (int): square glyph atlas size (default `512`, validated `[128..4096]`)
  - `color_rgba` (array): `[r,g,b]` or `[r,g,b,a]` (0–255, default `[255,255,255,255]`)
  - `opacity` (number): multiplies alpha, clamped to `[0..1]` (default `1.0`)
  - `x_px` (int): anchor x in internal framebuffer pixels (default: `fb.width/2`)
  - `y_px` (int): anchor y in internal framebuffer pixels (default: `fb.height/2`)
  - `scroll` (bool): vertical scroll upwards over time (default `false`)
  - `scroll_speed_px_s` (number): pixels/sec (default `30.0`)

- `fade_in` (object)
  - `duration_ms` (int): fade duration, `>0` enables fade
  - `from_rgba` (array): `[r,g,b]` or `[r,g,b,a]` overlay color

- `fade_out` (object)
  - Same shape as `fade_in`

## End conditions (REQUIRED)

A Scene must define at least one end condition. The loader refuses to run a scene without one.

`end` object:

- `timeout_ms` (int, optional): scene requests exit after this many milliseconds.
- `any_key` (bool, optional): if true, any non-repeated keypress triggers exit.
- `key` (string, optional): specific key to end on, using SDL scancode names (e.g. `"Space"`, `"Return"`, `"Escape"`).

Note: by default, `"Escape"` is also used to release mouse capture. If mouse capture is engaged, the first Escape press releases the mouse and is consumed; press Escape again to trigger a scene end condition that is bound to Escape.

Validation rules:

- At least one of:
  - `timeout_ms > 0`, or
  - `any_key == true`, or
  - `key` present and valid

If multiple end conditions are set, the first one satisfied triggers exit.

## Runtime behavior

### Background

- Loads the PNG once on screen enter.
- Draws it scaled (nearest-neighbor) to the internal framebuffer resolution (`cfg.render.internal_width/height`).

### Text

- Uses the existing TrueType text system (`FontSystem`) and `font_draw_text()`.
- SceneScreen owns its own `FontSystem` initialized from `text.font` and `text.size_px`.

### Music

- On enter (if configured and audio/music enabled):
  - `midi_stop()`
  - `midi_init(soundfont)`
  - `midi_play(midi)`
- On exit: if Scene started music, it calls `midi_stop()`.

If no Scene MIDI is configured:

- Default: the Scene stops any currently playing MIDI on enter.
- If `music.no_stop` is true: the Scene does **not** stop currently playing MIDI.

If a map is currently loaded and has background music configured, the engine will attempt to resume the map's MIDI when returning to gameplay after the Scene completes.

### SFX

- `enter_wav` plays once on enter.
- `exit_wav` plays once on exit.

### Fades

Fades are applied as a full-screen alpha-blended overlay **after** drawing the scene background and text.

- Fade-in: at `t=0`, overlay is `from_rgba`; linearly fades to transparent over `duration_ms`.
- Fade-out: once an end condition triggers, linearly fades from transparent to `from_rgba` over `duration_ms`.
- Scene completes when fade-out (if enabled) finishes; otherwise it completes immediately on end trigger.

## Console + CLI usage

- Console:
  - `load_scene <scene_file>`
  - `<scene_file>` is resolved under `Assets/Scenes/`.
  - `load_scene` does **not** unload the currently loaded map; it runs the Scene as the active screen while gameplay update/render is suspended.

- CLI:
  - `./build/mortum --scene <scene_file>`
  - Runs the scene as the active screen and exits cleanly when it completes.

## Code pointers (authoritative)

- Screen interface/runtime:
  - include/game/screen.h
  - include/game/screen_runtime.h
  - src/game/screen_runtime.c

- Scene asset model/loader:
  - include/assets/scene.h
  - include/assets/scene_loader.h
  - src/assets/scene_loader.c

- Scene screen implementation:
  - include/game/scene_screen.h
  - src/game/scene_screen.c

- Scaled background blit helper:
  - include/render/draw.h
  - src/render/draw.c

- Entry points:
  - src/main.c (`--scene`)
  - src/game/console_commands.c (`load_scene`)
