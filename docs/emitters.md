# Emitters (Light + Sound)

This document covers *all* emitter systems currently in Mortum:

- **Sound emitters**: the gameplay-facing SFX entry point (one-shots + looping ambient sources).
- **Light emitters**: point lights used by the software renderer lighting model.

The **header files** are treated as the API source of truth, and the **.c implementations** are treated as the behavioral source of truth.

---

## At a Glance

### Sound emitters

- Public API: [include/game/sound_emitters.h](../include/game/sound_emitters.h)
- Implementation: [src/game/sound_emitters.c](../src/game/sound_emitters.c)
- Underlying SFX core: [include/platform/audio.h](../include/platform/audio.h), [src/platform/audio_sdl.c](../src/platform/audio_sdl.c)
- Map-authored definitions: [include/assets/map_loader.h](../include/assets/map_loader.h), [src/assets/map_loader.c](../src/assets/map_loader.c)
- Runtime integration:
  - Map spawn + frame update: [src/main.c](../src/main.c)
  - Gameplay one-shots: [src/game/weapons.c](../src/game/weapons.c), [src/game/sector_height.c](../src/game/sector_height.c)

### Light emitters (point lights)

- Public API + data model: [include/render/lighting.h](../include/render/lighting.h)
- World storage + runtime light management: [include/game/world.h](../include/game/world.h), [src/game/world.c](../src/game/world.c)
- Rendering integration + culling + flicker: [include/render/raycast.h](../include/render/raycast.h), [src/render/raycast.c](../src/render/raycast.c)
- Map-authored definitions + validation:
  - Load: [src/assets/map_loader.c](../src/assets/map_loader.c)
  - Validate: [src/assets/map_validate.c](../src/assets/map_validate.c)

---

## Sound Emitters

### Conceptual model

The sound emitter system is the **single gameplay-facing path for SFX** (per the comment contract in `include/game/sound_emitters.h`).

Emitters can be:

- **Spatial** (world-space): output gain attenuates based on distance to a listener point.
- **Non-spatial** (player/camera UI-ish): no distance attenuation.

There are two usage styles:

1. **One-shot playback** without creating a persistent emitter.
2. **Looping playback** on a persistent emitter, updated each frame to track distance attenuation.

> Note: the emitter system does *not* implement stereo panning, occlusion, or reverb. It’s strictly gain attenuation and looping control.

### Data structures (API surface)

From [include/game/sound_emitters.h](../include/game/sound_emitters.h):

- `SoundEmitterId { uint16_t index; uint16_t generation; }`
  - This is an opaque-ish handle returned to gameplay.
- `SoundEmitters`
  - A fixed-capacity pool (`SOUND_EMITTER_MAX`, currently 256).
  - Stores per-emitter position, spatial flag, base gain, and (optional) loop playback handles.

Although `SoundEmitters` fields are publicly visible in the header, **treat them as internal**—behavioral invariants rely on the pool bookkeeping.

### Lifetime + pool semantics (implementation truth)

From [src/game/sound_emitters.c](../src/game/sound_emitters.c):

- `sound_emitters_init`:
  - Zeroes the struct.
  - Initializes a free list over `[0..SOUND_EMITTER_MAX-1]`.
  - Sets every slot’s generation to `1`.
  - Marks emitters dead.
- `SoundEmitterId` validity:
  - `{0,0}` is treated as an invalid “null id”.
  - A non-zero id is still invalid if:
    - `index` out of range
    - slot not alive
    - generation mismatch
- `sound_emitter_destroy`:
  - Stops an active loop if present.
  - Marks the slot dead.
  - Increments the slot generation.
  - Pushes the slot back to the free list.

**Actionable invariant**: never store a `SoundEmitterId` beyond the lifetime of the owning `SoundEmitters` pool (or across `sound_emitters_reset` / `sound_emitters_shutdown`). The pool is memset to zero on shutdown.

**Generation wraparound note**: generation is `uint16_t`. Extremely high-frequency create/destroy cycles could (in theory) wrap and allow a stale id to match again. If that ever becomes a concern, widening generation to 32-bit is the usual fix.

### Gain + attenuation model (implementation truth)

The attenuation function lives in `sound_emitters.c` (`attenuate_gain`). Behavior:

- `base_gain` is clamped to `[0,1]`.
- If `spatial == false`, gain is `base_gain`.
- If `spatial == true`:
  - Distance $d$ is computed in 2D ($x,y$).
  - There is a near region with no attenuation, and a far region that reaches 0:
    - `min_d = cfg->audio.sfx_atten_min_dist` (default 6.0)
    - `max_d = cfg->audio.sfx_atten_max_dist` (default 28.0)
    - Defaults are also mirrored in [include/game/tuning.h](../include/game/tuning.h) (`SFX_ATTEN_MIN_DIST`, `SFX_ATTEN_MAX_DIST`) as fallback when config is absent.
  - If `max_d <= min_d`, attenuation is effectively disabled (gain stays `base_gain`).
  - The falloff is quadratic in normalized distance: $g = base\_gain \cdot t^2$ where $t \in [0,1]$.

### API reference (exhaustive)

All functions below are declared in [include/game/sound_emitters.h](../include/game/sound_emitters.h).

#### `void sound_emitters_init(SoundEmitters* self)`

- Initializes the pool. Safe to call on a stack-allocated `SoundEmitters`.
- Required before any other `sound_*` calls.

#### `void sound_emitters_shutdown(SoundEmitters* self)`

- Stops any active loops (`sfx_voice_stop` for each alive + `loop_active` slot).
- Clears the entire struct with `memset`.
- After shutdown, all ids become invalid and `self->initialized` becomes false.

#### `void sound_emitters_reset(SoundEmitters* self)`

- Equivalent to `shutdown` + `init`.
- Use this when loading a new map to guarantee all ambient loops are stopped.

#### `SoundEmitterId sound_emitter_create(SoundEmitters* self, float x, float y, bool spatial, float base_gain)`

- Allocates from the free list.
- Initializes per-emitter fields.
- Returns `{0,0}` on failure (uninitialized pool, full pool, invalid args).
- `base_gain` is clamped to `[0,1]`.

#### `void sound_emitter_destroy(SoundEmitters* self, SoundEmitterId id)`

- No-op if `id` is invalid/stale.
- Stops an active loop for that emitter.

#### `void sound_emitter_set_pos(SoundEmitters* self, SoundEmitterId id, float x, float y)`

- No-op if `id` is invalid/stale.

#### `void sound_emitter_set_gain(SoundEmitters* self, SoundEmitterId id, float base_gain)`

- No-op if `id` is invalid/stale.
- `base_gain` is clamped to `[0,1]`.

#### `void sound_emitters_play_one_shot_at(SoundEmitters* self, const char* wav_filename, float x, float y, bool spatial, float base_gain, float listener_x, float listener_y)`

- Plays a one-shot sample at an arbitrary position.
- Behavioral notes from [src/game/sound_emitters.c](../src/game/sound_emitters.c):
  - `self` is currently unused (`(void)self;`). The call is effectively a thin wrapper over the SFX core.
  - Internally calls:
    - `sfx_load_effect_wav(wav_filename)`
    - `sfx_play(sample, computed_gain, looping=false)`
  - If sample load fails or audio is disabled, it becomes a silent no-op.

**Actionable tip**: use this for weapon shots, UI clicks, and transient impacts; prefer emitter+loop for ambient/hum sounds.

#### `void sound_emitter_start_loop(SoundEmitters* self, SoundEmitterId id, const char* wav_filename, float listener_x, float listener_y)`

- Loads the WAV sample and starts a looping voice.
- Stores both `loop_sample` and `loop_voice` in the emitter slot.
- Initializes loop gain based on listener distance.
- `loop_active` is set only if `sfx_play` returns a non-zero `SfxVoiceId`.

#### `void sound_emitter_stop_loop(SoundEmitters* self, SoundEmitterId id)`

- No-op if `id` is invalid/stale or the emitter has no active loop.
- Calls `sfx_voice_stop` and clears `loop_active`.

#### `void sound_emitters_update(SoundEmitters* self, float listener_x, float listener_y)`

- Iterates all `SOUND_EMITTER_MAX` slots.
- For each alive slot with an active loop:
  - Recomputes attenuated gain.
  - Calls `sfx_voice_set_gain`.

**Performance note**: this is an O(256) loop every frame, regardless of active count. That’s usually fine; if `SOUND_EMITTER_MAX` grows, consider tracking an active list.

### Underlying SFX core (important for emitter behavior)

From [include/platform/audio.h](../include/platform/audio.h) and [src/platform/audio_sdl.c](../src/platform/audio_sdl.c):

- `sfx_init(paths, enable_audio, freq, samples)`
  - If `enable_audio == false`, all SFX calls become no-ops that “succeed” without playing sound.
- `sfx_load_effect_wav(filename)`
  - Loads and caches WAV samples from `Assets/Sounds/Effects/<filename>`.
  - Returns `{0,0}` on failure.
  - Has a finite cache (`SFX_MAX_SAMPLES`), and logs a warning when full.
- `sfx_play(sample, gain, looping)`
  - Allocates a voice slot (`SFX_MAX_VOICES`). If full, steals the oldest voice.
- `sfx_voice_set_gain` / `sfx_voice_stop`
  - Safe to call with invalid/stale `SfxVoiceId`.
  - Updates are guarded with `SDL_LockAudioDevice`, so voice mutations are thread-safe relative to the audio callback.

### Integration: where emitters are created/updated

#### Map-authored ambient sound emitters

Map JSON may include a `"sounds"` array, parsed into `MapLoadResult.sounds` / `sound_count` (see [include/assets/map_loader.h](../include/assets/map_loader.h) and [src/assets/map_loader.c](../src/assets/map_loader.c)).

Runtime creation happens in [src/main.c](../src/main.c):

- On successful map load:
  - `sound_emitters_reset(&sfx_emitters)`
  - For each `MapSoundEmitter`:
    - `SoundEmitterId id = sound_emitter_create(...)`
    - If `loop == true`, start looping: `sound_emitter_start_loop(...)`

Per-frame maintenance also happens in `main.c`:

- Each frame after camera is computed:
  - `sound_emitters_update(&sfx_emitters, cam.x, cam.y)`

#### Gameplay one-shots

Examples of one-shot usage:

- Weapon fire: [src/game/weapons.c](../src/game/weapons.c)
- Toggle wall sounds: [src/game/sector_height.c](../src/game/sector_height.c)

These use `sound_emitters_play_one_shot_at(...)` with either `spatial=true` (world events) or `spatial=false` (player-centric).

### Map authoring: `sounds[]` schema

Parsed in `map_loader.c` (see the “optional sound emitters” section).

Each entry is an object with:

- Required:
  - `x` (number)
  - `y` (number)
  - `sound` (string): WAV filename under `Assets/Sounds/Effects/`
- Optional:
  - `loop` (bool, default `false`)
  - `spatial` (bool, default `true`)
  - `gain` (number clamped to `[0,1]`, default `1.0`)

Example:

```json
{
  "sounds": [
    {"x": 12.0, "y": 6.5, "sound": "Buzz_Loop.wav", "loop": true, "spatial": true, "gain": 0.6},
    {"x": 0.0, "y": 0.0, "sound": "UI_Click.wav", "loop": false, "spatial": false, "gain": 0.8}
  ]
}
```

---

## Light Emitters (Point Lights)

### Conceptual model

Point lights are authored and/or spawned as `PointLight` structs and stored on the `World`. The renderer uses them to **additively brighten** the base sector lighting multipliers.

They are “emitters” in the sense that they emit light into nearby shading samples, but there is no separate runtime system object—**the `World` owns the light list**.

### Data model (API surface)

From [include/render/lighting.h](../include/render/lighting.h):

- `LightColor { float r, g, b; }`
- `LightFlicker { NONE, FLAME, MALFUNCTION }`
- `PointLight`:
  - `x,y,z`: world position
  - `radius`: effective 2D influence radius (lighting checks use x/y only)
  - `intensity`: brightness in `[0,+inf)` (clamped by shader math later)
  - `color`: RGB multiplier; inputs are clamped to `[0,1]` at shading time
  - `flicker`: optional time-varying intensity modulation
  - `seed`: controls flicker randomness

### Storage + runtime light management

From [include/game/world.h](../include/game/world.h) and [src/game/world.c](../src/game/world.c):

- `World` stores:
  - `PointLight* lights` (owned)
  - `int light_count`
  - `int light_capacity`

Allocation + mutation API:

- `bool world_alloc_lights(World* self, int count)`
  - Frees any existing `lights` array and allocates exactly `count` lights.
  - Initializes default `color = white`, `flicker = none`, `seed = 0`.
- `int world_light_spawn(World* self, PointLight light)`
  - Appends a light, growing capacity (doubling strategy) as needed.
  - Returns the inserted light index, or `-1` on failure.
- `bool world_light_remove(World* self, int light_index)`
  - Removes by swapping the last light into `light_index` and decrementing `light_count`.
  - **Important**: removal is not stable; indices after removals may refer to different lights.
- `bool world_light_set_pos(World* self, int light_index, float x, float y, float z)`
- `bool world_light_set_intensity(World* self, int light_index, float intensity)`

**Actionable invariant**: if gameplay stores light indices, it must tolerate index instability after `world_light_remove`. If you need stable identities, introduce a handle+generation scheme similar to `SoundEmitterId`.

### Shading behavior (lighting model)

From [src/render/lighting.c](../src/render/lighting.c):

- `lighting_distance_falloff(dist)` computes a fog-to-black factor based on config:
  - `render.lighting.enabled` can disable distance falloff.
  - `fog_start` / `fog_end` define a quadratic fade.
- `lighting_compute_multipliers(...)`:
  - Computes an ambient baseline from sector light intensity:
    - `amb = clamp(sector_intensity,0..1) * ambient_scale * fog`
  - Applies sector tint per channel.
  - Adds point light contributions (still fogged):
    - For each light within radius in 2D:
      - $t = 1 - d/r$
      - contribution $a = intensity * clamp(t^2,0..1)$
      - adds `a * color` to multipliers
  - Clamps multipliers to `[0,1]` and lifts a fogged minimum visibility floor (`min_visibility * fog`).
- `lighting_quantize_factor(v)` optionally band-limits multipliers to `quantize_steps`, preserving values below `quantize_low_cutoff`.
- `lighting_apply(...)` multiplies the pixel RGB by quantized multipliers.

### Rendering integration: culling, caps, flicker

Point lights are *not* applied directly from `World.lights` each pixel. Instead, the renderer builds a per-frame visible light list.

From [src/render/raycast.c](../src/render/raycast.c):

- Global switch: `raycast_set_point_lights_enabled(bool)` toggles the entire point-light processing path.
- Visible list building:
  - Hard cap: `MAX_VISIBLE_LIGHTS` (currently 96) after view culling.
  - View culling includes:
    - max distance check: `d <= 32 + radius`
    - FOV check with a +12° margin; lights inside their radius are kept even if outside view.
  - Flicker is applied during visible-list building by scaling `PointLight.intensity`:
    - `LIGHT_FLICKER_FLAME`: smooth value noise around ~8 Hz
    - `LIGHT_FLICKER_MALFUNCTION`: mostly-on with occasional off/strobe
- Per-surface caps (to bound per-pixel work):
  - Walls: `MAX_ACTIVE_LIGHTS_WALLS` (8)
  - Planes (floors/ceilings): `MAX_ACTIVE_LIGHTS_PLANES` (6)
  - Caps are applied by scoring lights relative to the camera (`light_score_for_camera`) and keeping top-N.

**Performance detail for planes**: floor/ceiling lighting uses a `light_step` of 4 pixels vertically in a column; it recomputes multipliers every 4 pixels and reuses them in between (see `draw_sector_floor_column` / `draw_sector_ceiling_column`).

### Integration: where lights come from

#### Map-authored point lights (`lights[]`)

Maps may contain a `"lights"` array.

- Load: [src/assets/map_loader.c](../src/assets/map_loader.c) under “optional point lights”.
- Validate: [src/assets/map_validate.c](../src/assets/map_validate.c) checks non-negative radius/intensity and warns if a light is outside any sector.

Schema per entry:

- Required:
  - `x` (number)
  - `y` (number)
  - `radius` (number; negative becomes 0)
  - `brightness` **or** `intensity` (number; negative becomes 0)
- Optional:
  - `z` (number, default 0)
  - `color`:
    - either an object `{ "r": <float>, "g": <float>, "b": <float> }`
    - or a hex string `"RRGGBB"` / `"#RRGGBB"` (converted to 0..1 floats)
  - `flicker`: one of `"none"`, `"flame"`, `"malfunction"`
  - `seed` (int). If omitted, a derived seed is generated from authored properties.

Example:

```json
{
  "lights": [
    {"x": 10.0, "y": 12.0, "z": 1.4, "radius": 6.5, "intensity": 0.9, "color": "FFCC88", "flicker": "flame"},
    {"x": 22.0, "y":  4.0, "radius": 4.0, "brightness": 0.6, "color": {"r": 0.2, "g": 0.4, "b": 1.0}, "flicker": "none", "seed": 12345}
  ]
}
```

#### Runtime/programmatic lights

Gameplay can create lights at runtime using the `world_light_*` API in [include/game/world.h](../include/game/world.h).

Typical pattern:

1. Create a `PointLight` (usually with `color = light_color_white()` and `flicker = LIGHT_FLICKER_NONE`).
2. Spawn it: `int idx = world_light_spawn(&world, light)`.
3. Optionally update it: `world_light_set_pos`, `world_light_set_intensity`.
4. Remove it when done: `world_light_remove(&world, idx)`.

---

## Debugging + Troubleshooting

### Sound

- If nothing plays:
  - Confirm `audio.enabled` is true and `sfx_init` succeeded (see startup log in [src/main.c](../src/main.c)).
  - Confirm the WAV exists under `Assets/Sounds/Effects/`.
  - Check `sfx_master_volume` and per-sound `gain`/`base_gain`.
- If loops never stop:
  - Ensure `sound_emitters_reset` is called when changing maps (it is in `main.c`).
  - Ensure emitters are destroyed or `sound_emitter_stop_loop` is invoked when appropriate.

### Lights

- If point lights appear to do nothing:
  - Confirm `render.point_lights_enabled` is true in config and/or point lights weren’t toggled off at runtime.
  - Runtime toggle: default key `K` (`input.toggle_point_lights`), implemented in [src/main.c](../src/main.c).
- If lighting cost is high:
  - Use the perf trace overlay/capture; `RaycastPerf` tracks `lighting_apply_calls` and `lighting_apply_light_iters` (see [include/render/raycast.h](../include/render/raycast.h) and [src/render/raycast.c](../src/render/raycast.c)).
  - Reduce number of lights near the camera or radius sizes; the renderer caps active lights, but uncapped cull still iterates world lights.

---

## Extending the Systems Safely

### Adding new SFX behaviors

- If you add occlusion/panning:
  - Keep the public `sound_emitters_*` API stable; extend behavior internally in [src/game/sound_emitters.c](../src/game/sound_emitters.c).
  - Consider whether you need a listener orientation (not just position) and update function signatures accordingly.

### Adding new light flicker types

- Add an enum value in [include/render/lighting.h](../include/render/lighting.h).
- Update the JSON parser in [src/assets/map_loader.c](../src/assets/map_loader.c) (`json_get_light_flicker`).
- Implement the flicker function in [src/render/raycast.c](../src/render/raycast.c) and include it in the `switch (tmp.flicker)`.

### Making light identities stable

If gameplay starts storing light references for long periods, prefer a handle+generation scheme (like `SoundEmitterId`) instead of raw indices, because `world_light_remove` swaps elements.
