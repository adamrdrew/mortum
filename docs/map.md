# Mortum Map System (Levels)

This document describes **how the Mortum map system works end-to-end**, including:

- The **Level JSON format** (what fields exist, defaults, and strict validation rules).
- The **runtime data model** (`World`, walls/sectors/lights, etc.).
- How maps integrate into the **main game loop** (rendering, collision, sector queries, triggers).
- The **APIs and extension points** you will touch to add new map-driven features.

It is written to be **LLM/developer-agent friendly**: concrete schemas, explicit invariants, and the actual code entry points that implement behavior.

---

## Source of truth (read these first)

The authoritative implementations for maps are:

- Loading/parsing: `map_load()` in `include/assets/map_loader.h` implemented by `src/assets/map_loader.c`
- Validation: `map_validate()` in `include/assets/map_validate.h` implemented by `src/assets/map_validate.c`
- Runtime world model + helpers: `include/game/world.h` implemented by `src/game/world.c`

Primary runtime consumers:

- Player spawn selection: `episode_runner_apply_level_start()` in `src/game/episode_runner.c`
- Collision: `collision_move_circle()` and `collision_line_of_sight()` in `src/game/collision.c`
- Rendering (portal raycaster, lighting, sky): `raycast_render_textured*()` in `src/render/raycast.c`
- Movable floors (toggle walls): `sector_height_try_toggle_touching_wall()` / `sector_height_update()` in `src/game/sector_height.c`
- Map-authored emitters instantiation: `src/main.c` (spawns sound/particle emitters from `MapLoadResult`)
- Map-authored entity placements: `entity_system_spawn_map()` in `src/game/entities.c`

There are also historical/spec artifacts you may see referenced:

- `docs/map_prompt.md` (LLM generation prompt; useful but not fully complete)
- `specs/**/contracts/map.schema.json` and related docs (may be outdated vs engine code)

---

## Quick concepts / glossary

- **Map file**: a JSON file under `Assets/Levels/` (example: `Assets/Levels/mortum_test.json`).
- **World**: the runtime geometry container holding vertices, sectors, walls, and point lights.
- **Vertex**: a 2D point `(x,y)`.
- **Sector**: a region in 2D space with floor/ceiling height and textures.
- **Wall**: a directed edge `v0 -> v1` that belongs to `front_sector`. If it has `back_sector != -1`, it is a portal between two sectors.
- **Solid wall**: `back_sector == -1`.
- **Portal wall**: `back_sector >= 0`.
- **“Inside a sector”**: determined by an even-odd point-in-polygon test over that sector’s *front-side* wall edges.

---

## End-to-end lifecycle

### 1) Load and validate (startup / level change)

Typical flow (matches `src/main.c`):

1. Resolve assets base paths into an `AssetPaths` instance.
2. Call `map_load(&map, &paths, map_filename)` where `map_filename` is **relative to `Assets/Levels/`**.
3. On success, the `MapLoadResult` contains:
   - `World world` (geometry + lights + world-owned particle pool)
   - `player_start_*` fields
   - optional `bgmusic`, `soundfont`, `sky`
   - optional map-authored `sounds`, `particles`, `entities`
4. `map_load()` calls `map_validate()`; load fails if validation fails.
5. `map_load()` also builds an acceleration structure via `world_build_sector_wall_index(&world)`.

### 2) Runtime usage (per frame)

Once a map is loaded:

- The player is placed via `episode_runner_apply_level_start(&player, &map)`.
- Movement/collision uses `World.walls` for solid walls.
- Rendering uses `World` for raycasting and portal recursion.
- Optional “map auth” systems are spawned from `MapLoadResult`:
  - `map.sounds[]` → `SoundEmitters` loop emitters (ambient audio)
  - `map.particles[]` → `ParticleEmitters` (author-defined particle definitions)
  - `map.entities[]` → `EntitySystem` spawns actual runtime entities

---

## Runtime data model

### `MapLoadResult`

Defined in `include/assets/map_loader.h`.

Key ownership rules:

- `MapLoadResult.world` is owned by the result; destroy with `map_load_result_destroy()`.
- `sounds`, `particles`, `entities` arrays are heap-allocated and owned by the result.

Fields:

- `World world`
- `float player_start_x`, `player_start_y`, `player_start_angle_deg`
- `char bgmusic[64]`: MIDI filename (under `Assets/Sounds/MIDI/`)
- `char soundfont[64]`: SoundFont filename (under `Assets/Sounds/SoundFonts/`)
- `char sky[64]`: optional sky panorama filename (under `Assets/Images/Sky/`)
- `MapSoundEmitter* sounds`, `int sound_count`
- `MapParticleEmitter* particles`, `int particle_count`
- `MapEntityPlacement* entities`, `int entity_count`

### `World`

Defined in `include/game/world.h`.

- `Vertex* vertices`, `int vertex_count`
- `Sector* sectors`, `int sector_count`
- `Wall* walls`, `int wall_count`
- Optional acceleration structure built after load:
  - `sector_wall_offsets`, `sector_wall_counts`, `sector_wall_indices`
  - Built by `world_build_sector_wall_index()`
- `PointLight* lights` + bookkeeping (`light_alive`, freelist, etc.)
- `Particles particles` (world-owned particle pool)

Important: `World` contains only the data. Runtime systems (player, entities, emitters, renderer) use it.

### `Sector`

Defined in `include/game/world.h`:

- `id` (int): map-authored identifier, **not** an index.
- Heights:
  - `floor_z`: runtime floor height
  - `floor_z_origin`: authored floor height
  - `floor_z_toggled_pos`: alternate floor height for toggle floors
  - `ceil_z`
- Movable floor state:
  - `movable`, `floor_moving`, `floor_z_target`, `floor_toggle_wall_index`
- Lighting:
  - `light` (scalar ambient)
  - `light_color` (RGB tint)
- Textures:
  - `floor_tex[64]`, `ceil_tex[64]`

### `Wall`

- Geometry:
  - `v0`, `v1` (vertex indices)
  - `front_sector` (index)
  - `back_sector` (index or `-1` for solid)
- Textures:
  - `tex[64]`: current wall texture (may change at runtime)
  - `base_tex[64]`: original map texture
  - `active_tex[64]`: optional alternate texture when a toggle sector is “active”
- Toggle behavior:
  - `toggle_sector` (bool)
  - `toggle_sector_id` (int): **sector `id`**, not sector index; `-1` means default
  - `toggle_sector_oneshot` (bool)
  - `toggle_sound[64]`, `toggle_sound_finish[64]` (WAV under `Assets/Sounds/Effects/`)

---

## Map JSON format

Maps are JSON objects. `map_load()` requires specific fields and is strict.

### File location and name

- Loaded from `Assets/Levels/<map_filename>` via `asset_path_join(paths, "Levels", map_filename)`.
- Many filenames are copied into fixed `char[64]` buffers. In practice, keep filenames **≤ 63 bytes**.

### Top-level object

#### Required fields

- `player_start` (object)
- `vertices` (array)
- `sectors` (array)
- `walls` (array)

#### Optional fields (actively used)

- `bgmusic` (string): MIDI filename under `Assets/Sounds/MIDI/`.
  - Default if missing: empty string (disabled).
- `soundfont` (string): SoundFont filename under `Assets/Sounds/SoundFonts/`.
  - Default if missing: `"hl4mgm.sf2"`.
- `sky` (string): sky panorama filename under `Assets/Images/Sky/`.
  - Default if missing: empty string.
- `lights` (array): point lights (see below)
- `sounds` (array): map-authored sound emitters
- `particles` (array): map-authored particle emitters
- `entities` (array): map-authored entity placements

#### Permitted-but-ignored fields

These may exist in older maps or tools; loader ignores them:

- `version` (number)
- `name` (string)
- `textures` (object)
- `flags` on walls (array)

Do not rely on ignored fields for gameplay.

---

## Schema: `player_start` (required)

Object fields:

- `x` (number)
- `y` (number)
- `angle_deg` (number)

Rules:

- `(x,y)` must be inside at least one sector.
- `map_validate()` requires **the spawn sector is reachable** from the portal graph.
- At runtime spawn selection may choose the “best” sector if multiple overlap (see “Overlapping sectors”).

---

## Schema: `vertices` (required)

Array of objects:

- `x` (number)
- `y` (number)

Rules:

- Must have at least 3 vertices.
- Walls refer to vertices by index; indices must be in-range.

---

## Schema: `sectors` (required)

Array of objects. **Walls reference sectors by array index**, not by `id`.

Required fields per sector:

- `id` (integer)
- `floor_z` (number)
- `ceil_z` (number)
- `floor_tex` (string)
- `ceil_tex` (string)
- `light` (number)

Optional fields:

- `light_color` (object): `{ "r": number, "g": number, "b": number }`
- `movable` (boolean)
- `floor_z_toggled_pos` (number)

Defaults and implied behavior:

- If `movable` is missing, it defaults to `false`.
- If `floor_z_toggled_pos` is present, the sector is treated as movable even if `movable` is omitted.
- If `movable` is `true`, `floor_z_toggled_pos` **must** be present.

Validation rules:

- `ceil_z > floor_z`
- `floor_tex` and `ceil_tex` must be non-empty strings.
- Movable sectors must have clearance: `ceil_z > max(floor_z_origin, floor_z_toggled_pos) + 0.10`.
- Each sector must have at least one **closed boundary loop** formed from its walls (see “Sector boundary validity”).

Validator warnings you may see:

- `Sector N has X wall components that are not closed loops (internal segments?)`
  - Meaning: besides the sector’s main boundary loop(s), there are additional connected components made from that sector’s `front_sector` wall edges whose vertices don’t all have degree 2 (i.e., not a cycle).
  - This is often accidental “stray” geometry, but it can also be intentional internal segments.
- `Sector N has X closed loops (obstacles/holes?)`
  - Meaning: the sector’s walls form multiple disjoint closed cycles.
  - The validator treats this as a warning (not a hard error). Containment tests in the validator pick the *largest* closed loop so obstacle/hole loops don’t break point-in-sector checks.

---

## Schema: `walls` (required)

Array of objects.

Required fields per wall:

- `v0` (integer)
- `v1` (integer)
- `front_sector` (integer sector index)
- `back_sector` (integer sector index, or `-1`)
- `tex` (string)

Optional fields (toggle floors)

- `toggle_sector` (boolean)
- `toggle_sector_id` (integer): refers to a **sector `id`**, not a sector index
- `toggle_sector_oneshot` (boolean)
- `active_tex` (string)
- `toggle_sound` (string): WAV filename under `Assets/Sounds/Effects/`
- `toggle_sound_finish` (string): WAV filename under `Assets/Sounds/Effects/`

Semantics:

- **Solid wall**: `back_sector == -1`
  - Blocks collision (`collision_move_circle`) and blocks LOS (`collision_line_of_sight`).
  - In the renderer, treated as an occluding wall.
- **Portal wall**: `back_sector >= 0`
  - Does not block collision or LOS.
  - In the renderer, acts as a portal between sectors; the raycaster can recurse through an “open span”.

Validation rules:

- `v0` and `v1` must be valid vertex indices and must not be equal.
- `front_sector` must be in range.
- `back_sector` must be `-1` or in range.
- `tex` must be non-empty.
- If `toggle_sector` is true and `toggle_sector_id != -1`, that `toggle_sector_id` must match some sector’s `id`.

---

## Schema: `lights` (optional)

Array of point-light objects (stored in `World.lights`).

Required:

- `x` (number)
- `y` (number)
- `radius` (number)
- Either `brightness` (number) or `intensity` (number)

Optional:

- `z` (number, default 0)
- `color`:
  - Hex string `"RRGGBB"` or `"#RRGGBB"`, **or**
  - Legacy object `{ "r": number, "g": number, "b": number }`
- `flicker` (string): `"none" | "flame" | "malfunction"`
- `seed` (integer): deterministic flicker seed

Loader behavior:

- `radius` and `intensity` are clamped to be non-negative.
- If `seed` is omitted, the engine derives one from `(i, x, y, radius)` to avoid synchronized flicker.

Validation behavior:

- Errors if `radius < 0` or `intensity < 0` (should be impossible due to loader clamp).
- Warns if a light is not inside any sector.

Renderer behavior:

- Visibility culling + flicker modulation happens per frame.
- There is a cap on how many point lights are considered per frame.

---

## Schema: `sounds` (optional)

Array of map-authored sound emitters (`MapSoundEmitter`). These are *definitions*; runtime audio is created by `SoundEmitters`.

Required:

- `x` (number)
- `y` (number)
- `sound` (string): WAV filename under `Assets/Sounds/Effects/`

Optional:

- `loop` (boolean, default `false`)
- `spatial` (boolean, default `true`)
- `gain` (number, default `1.0`, clamped to `[0,1]`)

Runtime instantiation (matches `src/main.c`):

- For each entry, create a `SoundEmitter` at `(x,y)`.
- If `loop` is true, start a looping WAV.
- Loop volume is updated using listener distance if `spatial` is true.

---

## Schema: `particles` (optional)

Array of map-authored particle emitters (`MapParticleEmitter`). These are *definitions*; runtime emitters are created in `ParticleEmitters`.

Required:

- `x` (number)
- `y` (number)
- `particle_life_ms` (integer)
- `emit_interval_ms` (integer)
- `start` (object)
- `end` (object)

Optional:

- `z` (number, default `0`)
- `offset_jitter` (number, default `0`)
- `rotate` (object)
- `image` (string): filename under `Assets/Images/Particles/` (no path)
- `shape` (string): `"circle" | "square"` (used when `image` is empty)

`start`/`end` keyframes:

- `opacity` (number)
- `size` (number)
- `offset` (object): `{ "x": number, "y": number, "z": number }`
- `color` (object):
  - `value` (string hex `RRGGBB` or `#RRGGBB`)
  - `opacity` (number, optional; used as blend opacity for image particles)

`rotate` object:

- `enabled` (boolean, default false)
- `tick` (object, optional):
  - `deg` (number, default 0)
  - `time_ms` (integer, default 30)

Runtime behavior:

- Map emitters are created with `particle_emitter_create(&particle_emitters, &world, x, y, z, &def)`.
- Emission can be gated by line-of-sight: emitters in a different sector may only emit if there is solid-wall LOS to the player.

---

## Schema: `entities` (optional)

Array of entity placements (`MapEntityPlacement`). These are spawn points; actual runtime entities are created by the entity system.

Required:

- `x` (number)
- `y` (number)

Preferred (current system):

- `def` (string): entity definition name (looked up in `Assets/Entities/entities.json`)

Optional:

- `yaw_deg` (number, default 0)

Legacy:

- `type` (string): legacy pre-entity-system value. The loader contains limited mappings.

Loader behavior:

- If `def` is missing/unrecognized, the placement is preserved but marked inactive (`def_name=""`, `sector=-1`).
- If `def` is present, the loader computes `sector = world_find_sector_at_point(world, x, y)` and fails the map load if the entity is not inside any sector.

Runtime behavior:

- `entity_system_spawn_map()` looks up each `def_name` in loaded defs and spawns entities.

---

## Geometry semantics and invariants

### Sector boundary validity (CRITICAL)

Validation uses a graph approach over the sector’s wall edges:

- A sector’s boundary is built from walls where `wall.front_sector == sector_index`.
- Walls are treated as **undirected edges** when analyzing closed loops.
- A component is considered a closed loop if every vertex in the component has degree 2 and the component has at least 3 vertices.

A sector must have **at least one** closed loop; otherwise the map is rejected.

Warnings:

- If a sector has non-loop components (open chains), validation logs a warning.
- If a sector has multiple loops (holes/obstacles), validation logs a warning.

Practical authoring rule:

- For each sector, define one simple polygon loop.
- For every portal edge shared by two sectors, add **two directed walls** (A→B and B→A) so both sectors can form valid boundaries using only their front-side walls.

### Inside/outside test

There are two related implementations:

- Runtime point-in-sector: `world_sector_contains_point()` does an even-odd test over walls with `front_sector == sector`.
- Validator containment: `map_validate.c` uses the **largest closed loop** for a sector when checking a point is inside it (to avoid internal segments or holes breaking the test).

Implication:

- Avoid holes and internal wall fragments; keep sectors simple.

### Overlapping sectors

Most gameplay assumes a point is inside one sector, but some maps intentionally overlap (raised platform inside room).

Spawn logic (`episode_runner_apply_level_start`) handles overlap by:

- considering all sectors that contain the spawn point,
- choosing the sector with the **highest floor** that still has enough headroom for the player body.

Other runtime queries (`world_find_sector_at_point`) return the **first** sector index whose even-odd test matches.

If you rely on overlapping sectors, you must design carefully; overlap can produce ambiguous sector selection for AI, emitters, etc.

---

## Runtime systems that consume map geometry

### Collision

- Solid walls (`back_sector == -1`) block collision and LOS.
- Portal walls do not block collision.

APIs:

- `collision_move_circle(world, radius, from, to)` pushes the circle out of solid walls and slides.
- `collision_line_of_sight(world, from, to)` tests LOS against solid walls (ignores portal walls).

Important implication:

- The game does not inherently prevent moving “outside” the map except via solid walls.
- If your sector boundary is not sealed with solid walls on the exterior, players/entities can escape into space not inside any sector.

### Rendering (raycaster)

The main renderer is a portal-capable raycaster.

- It chooses a “current sector” for the camera (stable across frames when near edges).
- For each ray/column, it finds the nearest wall intersection.
- If the hit wall is a portal, it:
  - draws upper/lower wall pieces relative to floor/ceil differences,
  - recurses through the open span into the back sector.

Acceleration structure:

- If `world_build_sector_wall_index()` has been called, the renderer can test only relevant walls per sector.

Sky rendering:

- If `sky_filename` is provided and a sector ceiling texture is `"SKY"` or `"sky"`, the ceiling is rendered with a cylindrical sky panorama loaded from `Assets/Images/Sky/`.

Point lights:

- Controlled by `raycast_set_point_lights_enabled(bool)`.
- Each frame, visible lights are culled and capped; flicker modulation is applied.

### Movable floors (toggle sectors)

Movable floors are a map-driven gameplay primitive.

Map-side:

- Sector: `movable=true` and `floor_z_toggled_pos`.
- Wall: `toggle_sector=true` to enable triggering.

Runtime behavior:

- Pressing the action key (`sector_height_try_toggle_touching_wall`) finds the nearest toggle wall the player is touching on the player’s side of the wall.
- The wall targets either:
  - the sector with `id == toggle_sector_id`, or
  - the player’s current sector if `toggle_sector_id == -1`.
- While any sector is moving, other toggles are blocked (global lock).
- When movement completes:
  - if the wall has `active_tex`, the wall texture can swap when the sector is at the toggled position,
  - optional `toggle_sound` and `toggle_sound_finish` are played at the wall midpoint.

Safety behavior:

- If moving the floor upward would crush the player into the ceiling, the movement is canceled.

---

## Validation rules (what can make a map fail to load)

`map_load()` fails if any of the following are not satisfied:

- Root JSON must be an object.
- Required fields exist and have the correct JSON types.
- `vertices.length >= 3`, `sectors.length >= 1`, `walls.length >= 1`.
- Every sector:
  - has `ceil_z > floor_z` and non-empty `floor_tex`/`ceil_tex`,
  - if movable, has sufficient ceiling clearance,
  - has at least one closed boundary loop.
- Every wall:
  - has valid vertex indices and `v0 != v1`,
  - has valid sector indices,
  - has non-empty `tex`.
- Toggle walls with `toggle_sector_id` must reference an existing sector `id`.
- Player start must be inside some sector.
- **Contiguity rule**: every sector must be reachable from the player’s start sector via the portal adjacency graph.
  - Any wall with `back_sector != -1` contributes an undirected edge between `front_sector` and `back_sector`.

Warnings (map can still load):

- Sectors with multiple closed loops or internal open components.
- Lights placed outside all sectors.

---

## Integration guide (how to use maps in code)

### Loading a map

Use `map_load()` and destroy with `map_load_result_destroy()`.

Pattern (simplified):

```c
MapLoadResult map;
if (!map_load(&map, paths, "mortum_test.json")) {
    // handle failure
}

// use map.world, map.player_start_x/y/angle

map_load_result_destroy(&map);
```

### After load: build derived structures

`map_load()` already calls `world_build_sector_wall_index()`. If you mutate walls/sectors after load (e.g., procedural generation), rebuild it:

```c
(void)world_build_sector_wall_index(&world);
```

### Level transitions

On level change (see `src/main.c` episode progression):

- Destroy previous map result.
- Load the new map.
- Rebuild any dependent structures (mesh).
- Apply player spawn.
- Reset/spawn dependent runtime systems (entities, emitters).

---

## Extending the map system safely

When you add a new map-driven feature, the standard pattern is:

1. **Add JSON fields** to the format (optional if you need backward compatibility).
2. Update `src/assets/map_loader.c` to parse them.
   - Prefer defaults when fields are omitted.
   - Validate JSON types strictly.
   - Keep filenames ≤ 63 bytes.
3. Update runtime structs as needed:
   - If it’s geometry-related, add to `World` / `Sector` / `Wall`.
   - If it’s a map-authored “definition list”, add to `MapLoadResult`.
4. Update `src/assets/map_validate.c` if the new feature imposes new invariants.
   - Keep validation conservative; reject maps only for truly invalid/unsafe states.
5. Update runtime systems to consume the new data.
6. Update authoring docs (`docs/map.md` and, if relevant, `docs/map_prompt.md`).

Common pitfalls when extending:

- Adding a field but forgetting to initialize defaults in `world_alloc_*` or `map_load()`.
- Adding a field to `Wall`/`Sector` but not updating `world_destroy()` (ownership).
- Introducing non-local invariants without validation, leading to hard-to-debug runtime issues.

---

## Minimal working example

A minimal map that loads (single sector rectangle, solid walls):

```json
{
  "player_start": {"x": 2.0, "y": 2.0, "angle_deg": 0},
  "vertices": [
    {"x": 0, "y": 0},
    {"x": 12, "y": 0},
    {"x": 12, "y": 12},
    {"x": 0, "y": 12}
  ],
  "sectors": [
    {"id": 0, "floor_z": 0, "ceil_z": 4, "floor_tex": "GRID_1A.PNG", "ceil_tex": "PANEL_2A.PNG", "light": 1.0}
  ],
  "walls": [
    {"v0": 0, "v1": 1, "front_sector": 0, "back_sector": -1, "tex": "BRICK_3A.PNG"},
    {"v0": 1, "v1": 2, "front_sector": 0, "back_sector": -1, "tex": "BRICK_3A.PNG"},
    {"v0": 2, "v1": 3, "front_sector": 0, "back_sector": -1, "tex": "BRICK_3A.PNG"},
    {"v0": 3, "v1": 0, "front_sector": 0, "back_sector": -1, "tex": "BRICK_3A.PNG"}
  ]
}
```
