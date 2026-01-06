# Gore system

This is the purpose-built gore/blood pipeline that sits **outside** the particle emitter stack. It owns its own pools,
ballistic simulation, and rendering so sticky splats never consume particle capacity and can be tuned independently of
sprite/quad particles.

---

## At a glance

- **Public API + data model**: [include/game/gore.h](../include/game/gore.h)
- **Implementation**: [src/game/gore.c](../src/game/gore.c)
- **Runtime integration**: gore lives on `World` ([include/game/world.h](../include/game/world.h)) and is initialized with
the map ([src/assets/map_loader.c](../src/assets/map_loader.c)). Gameplay spawns bursts during `ENTITY_EVENT_DAMAGE` and
`ENTITY_EVENT_DIED` handling ([src/main.c](../src/main.c)).

### Concepts

- **Chunks**: flying opaque squares that use simple ballistic physics, collide with world geometry only, and stamp when
they hit floors/ceilings/walls. They are pooled separately from stamps (`GORE_CHUNK_MAX_DEFAULT`).
- **Stamps**: persistent "sticky" splats that are floor-only in the current implementation. Chunk impacts only stamp
  onto floors (walls/ceilings are intentionally skipped due to view-dependent z-fighting and skybox edge cases).
  Each stamp procedurally scatters up to `GORE_STAMP_MAX_SAMPLES` droplets in the floor plane, with palette snapping and
  optional lifetime (0 = permanent).
- **Color palette**: four hard colors (white, pink, bright red, dark maroon) with spawn bias toward the two reds,
occasional pink, and rare white for specks. Live rendering never uses transparency.
- **Lighting**: both live chunks and baked stamps run through the lighting model (sector light and visible point lights)
when drawing, so gore respects scene illumination.
- **Determinism**: procedural sampling uses seeded xorshift RNG; zero/omitted seeds derive from position so splats are
stable given the same inputs.

---

## Data structures and lifetimes

### GoreSystem

`GoreSystem` owns two fixed pools: `items` (stamps) and `chunks` (flying gore). Its stats counters are reset per-frame by
`gore_begin_frame`. Pools are zeroed on `gore_reset`/`gore_shutdown` for deterministic cleanup.

- Initialization: `gore_init` allocates stamp capacity (defaults to `GORE_STAMP_MAX_DEFAULT`) and chunk capacity
  (`GORE_CHUNK_MAX_DEFAULT`). Failure tears down partial allocations.
- Reset/shutdown: `gore_reset` clears all live items, counters, and chunk state; `gore_shutdown` frees pools and zeroes
  the struct.

### Chunks (live gore)

- Structure: `GoreChunk` stores position, velocity, radius, palette-snapped color, age/life, and sector bookkeeping.
- Spawn: `gore_spawn_chunk` drops the newest request if the pool is full and defaults `life_ms` to ~2.8s when zero.
  It immediately snaps the requested RGB to the gore palette for consistency.
- Simulation: `gore_tick` advances gravity (`18.0f` units/s²), integrates XY motion with `collision_move_circle`, and
  handles Z floor/ceiling impacts. Floor hits stamp before killing the chunk; wall and ceiling hits currently do not
  stamp (the chunk is simply removed).
- Rendering: visible chunks draw as opaque squares in screen space, depth-tested against walls/depth buffers and lit per
  sector/point light.

### Stamps (sticky decals)

- Structure: `GoreStamp` stores world position, max radius, lifetime, and procedural droplet samples (`GoreSample`).
- Spawn: `gore_spawn` scatters up to `GORE_STAMP_MAX_SAMPLES` samples in the floor plane using a seeded RNG and snaps
  colors to the palette. New spawns drop when the pool is full.
- Tick: `gore_tick` ages stamps and culls any with finite lifetimes; persistent stamps keep `life_ms == 0`.
- Rendering: `gore_draw` projects each sample into screen space as an opaque square, clipping against wall depth/depth
  buffer and applying sector + point-light shading. A small depth bias is applied in the depth comparisons so decals
  reliably win against the surface they sit on (avoids close-up z-fighting). Draw stats (`stats_drawn_samples`,
  `stats_pixels_written`) are tracked for perf instrumentation.

---

## Gameplay integration

- **Map lifecycle**: `map_load` initializes both particles and gore; cleanup goes through `world_destroy` to release gore
  memory alongside other world-owned pools.
- **Frame loop**: `gore_begin_frame` clears stats each frame; `gore_tick` advances physics and lifetimes; `gore_draw`
renders after world geometry for both the main and mirror views.
- **Spawning events**: `src/main.c` wires gore bursts into gameplay:
  - Damage splatter: `gore_emit_damage_splatter` aims a mid-sized burst along the hit normal toward the player/body
    facing, using biased palette selection and chunky size tiers (1×/2×/4× with jitter) via `gore_emit_chunk_burst`.
  - Death burst: `gore_emit_death_burst` fires a heavier cone spread upward to shower the area before sticking.
  Both paths feed `gore_spawn_chunk`, letting physics handle arcs and impacts.

---

## Authoring/usage notes

- **Persistence**: stamps default to `life_ms = 0` (permanent). If you need temporary gore, pass a finite lifetime.
- **Avoid particle overlap**: use the gore API for sticky blood/gibs; it never consumes particle emitter slots.
- **Collision scope**: gore chunks ignore entities and only collide with world geometry. Ensure `last_valid_sector` is
  kept updated when spawning from moving actors for reliable floor/ceiling queries.
- **Color control**: pass any RGB; it will snap to the nearest palette entry. To bias distribution, adjust the palette
  roll thresholds in `gore_pick_palette` inside `src/main.c`.
- **Tuning spread/physics**: `gore_emit_chunk_burst` controls burst count, base speed, speed jitter, angular spread, and
  directional bias. Gravity and wall push distances live in `gore_tick` for quick iteration.

