# Mortum

Retro shooter project (C11 + software rendering) built with SDL2.

## Configuration

Mortum loads a single JSON config file on startup and (optionally) reloads it at runtime.

### Config file discovery

Path precedence (first match wins):

1. CLI: `--config <path>`
2. CLI: `CONFIG=<path>`
3. Env: `MORTUS_CONFIG=<path>`
4. `$HOME/.mortus/config.json` (only if it exists)
5. `./config.json` (only if it exists)

The repository includes a fully-populated default config at [config.json](config.json).

### Validation behavior

- Startup: invalid config is fatal (startup aborts and logs errors).
- Runtime reload (via in-game console `config_reload`): invalid config logs errors and keeps the previous config.
- Unknown keys: logged as warnings (forward-compatible).
- Asset references: validated on load.
	- `content.boot_timeline` must exist under `Assets/Timelines/`.
	- If `audio.enabled` is true, referenced WAV filenames must exist under `Assets/Sounds/Effects/`.

### Supported keys

Notes:

- **Reloadable** means changes take effect immediately after a runtime reload (via console `config_reload`) or an in-memory change (via console `config_change`).
- **Startup-only** means changes require a restart to take effect.
- Scancodes can be an SDL key name string (e.g. `"W"`, `"F3"`, `"Left Shift"`) or an int scancode.
- Some bindings accept either a single key or `[primary, secondary]`.

| JSON path | Type | Default | Reloadable | Notes |
|---|---:|---:|---:|---|
| `window.title` | string | `"Mortum"` | Startup-only | Window title |
| `window.width` | int | `1280` | Startup-only | Range: `[320..8192]` |
| `window.height` | int | `720` | Startup-only | Range: `[200..8192]` |
| `window.vsync` | bool | `true` | Startup-only | Affects SDL renderer present sync |
| `window.grab_mouse` | bool | `true` | Reloadable | SDL window grab |
| `window.relative_mouse` | bool | `true` | Reloadable | SDL relative mouse mode |
| `render.internal_width` | int | `640` | Startup-only | Range: `[160..4096]` |
| `render.internal_height` | int | `400` | Startup-only | Range: `[120..4096]` |
| `render.fov_deg` | number | `75` | Reloadable | Range: `[30..140]` |
| `render.point_lights_enabled` | bool | `true` | Reloadable | Also toggleable via keybind |
| `render.lighting.enabled` | bool | `true` | Reloadable | If false, disables fog + quantize |
| `render.lighting.fog_start` | number | `6` | Reloadable | Must satisfy `fog_end >= fog_start` |
| `render.lighting.fog_end` | number | `28` | Reloadable | Must satisfy `fog_end >= fog_start` |
| `render.lighting.ambient_scale` | number | `0.45` | Reloadable | Range: `[0..2]` |
| `render.lighting.min_visibility` | number | `0.485` | Reloadable | Range: `[0..1]` |
| `render.lighting.quantize_steps` | int | `16` | Reloadable | `<= 1` disables quantize |
| `render.lighting.quantize_low_cutoff` | number | `0.08` | Reloadable | Range: `[0..1]` |
| `ui.font.file` | string | `"ProggyClean.ttf"` | Startup-only | Filename under `Assets/Fonts/` |
| `ui.font.size` | int | `15` | Startup-only | Range: `[6..96]` |
| `ui.font.atlas_size` | int | `512` | Startup-only | Range: `[128..4096]` |
| `audio.enabled` | bool | `true` | Reloadable* | *MIDI playback is gated by this; SFX core is initialized at startup |
| `audio.sfx_master_volume` | number | `1` | Reloadable | Range: `[0..1]` |
| `audio.sfx_atten_min_dist` | number | `6` | Reloadable | Must satisfy `max_dist >= min_dist` |
| `audio.sfx_atten_max_dist` | number | `28` | Reloadable | Must satisfy `max_dist >= min_dist` |
| `audio.sfx_device_freq` | int | `48000` | Startup-only | Range: `[8000..192000]` |
| `audio.sfx_device_buffer_samples` | int | `1024` | Startup-only | Range: `[128..8192]` |
| `content.boot_timeline` | string | `"boot.json"` | Startup-only | Must exist in `Assets/Timelines/` |
| `input.bindings.forward` | key or `[key,key]` | `["W","Up"]` | Reloadable | Move forward |
| `input.bindings.back` | key or `[key,key]` | `["S","Down"]` | Reloadable | Move back |
| `input.bindings.left` | key or `[key,key]` | `"A"` | Reloadable | Strafe left |
| `input.bindings.right` | key or `[key,key]` | `"D"` | Reloadable | Strafe right |
| `input.bindings.dash` | key or `[key,key]` | `["Left Shift","Right Shift"]` | Reloadable | Dash/quick-step |
| `input.bindings.action` | key or `[key,key]` | `"Space"` | Reloadable | Wall/sector interact |
| `input.bindings.use` | key or `[key,key]` | `"F"` | Reloadable | Use (purge item) |
| `input.bindings.weapon_slot_1..5` | key | `"1".."5"` | Reloadable | Select weapon slots |
| `input.bindings.weapon_prev` | key | `"Q"` | Reloadable | Wheel decrement |
| `input.bindings.weapon_next` | key | `"E"` | Reloadable | Wheel increment |
| `input.bindings.toggle_debug_overlay` | key | `"F3"` | Reloadable | Toggles debug overlay |
| `input.bindings.toggle_fps_overlay` | key | `"P"` | Reloadable | Toggles FPS overlay |
| `input.bindings.toggle_point_lights` | key | `"K"` | Reloadable | Toggles point light emitters |
| `input.bindings.perf_trace` | key | `"O"` | Reloadable | Starts perf trace capture |
| `input.bindings.noclip` | key | `"F2"` | Reloadable | Toggles noclip |
| `player.mouse_sens_deg_per_px` | number | `0.12` | Reloadable | Range: `[0..10]` |
| `player.move_speed` | number | `4.7` | Reloadable | Range: `[0..100]` |
| `player.dash_distance` | number | `0.85` | Reloadable | Range: `[0..100]` |
| `player.dash_cooldown_s` | number | `0.65` | Reloadable | Range: `[0..60]` |
| `player.weapon_view_bob_smooth_rate` | number | `8` | Reloadable | Range: `[0..100]` |
| `player.weapon_view_bob_phase_rate` | number | `10` | Reloadable | Range: `[0..100]` |
| `player.weapon_view_bob_phase_base` | number | `0.2` | Reloadable | Range: `[0..10]` |
| `player.weapon_view_bob_phase_amp` | number | `0.8` | Reloadable | Range: `[0..10]` |
| `footsteps.enabled` | bool | `true` | Reloadable | Master toggle |
| `footsteps.min_speed` | number | `0.15` | Reloadable | Range: `[0..100]` |
| `footsteps.interval_s` | number | `0.35` | Reloadable | Range: `[0..10]` |
| `footsteps.variant_count` | int | `17` | Reloadable | Range: `[0..999]` |
| `footsteps.filename_pattern` | string | `"Footstep_Boot_Concrete-%03u.wav"` | Reloadable | Must include `%03u` placeholder |
| `footsteps.gain` | number | `0.7` | Reloadable | Range: `[0..1]` |
| `weapons.balance.*` | object | *(see config)* | Reloadable | Per-weapon numeric tuning |
| `weapons.view.shoot_anim_fps` | number | `30` | Reloadable | Range: `(0..240]` |
| `weapons.view.shoot_anim_frames` | int | `6` | Reloadable | Range: `[1..128]` |
| `weapons.sfx.*_shot` | string | *(see config)* | Reloadable | WAV filename under `Assets/Sounds/Effects/` |
| `weapons.sfx.shot_gain` | number | `1` | Reloadable | Range: `[0..1]` |

## Quickstart

Prereqs:
- C compiler (macOS: Xcode Command Line Tools)
- SDL2 development libraries (macOS: `brew install sdl2`)

Build/run:
- `make`
- `make run`

Dev:
- Open the in-game console (`` ` ``) and use `show_font_test true|false` to toggle the font smoke-test page

Docs:
- Architecture: `docs/ARCHITECTURE.md`
- Console: `docs/console.md`
- Contributing/build conventions: `docs/CONTRIBUTING.md`
- Emitters (sound + lights + particles): `docs/emitters.md`
- Entity system (including entity-attached lights/particles): `docs/entities.md`

Spec artifacts live under `specs/`.
