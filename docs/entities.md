# Entity System (Developer Documentation)

This document describes Mortum’s current entity system implementation: its public API, deterministic update rules, data formats (entity defs + map-authored spawns), and how the main loop integrates entity events.

Source of truth:
- Public API: [include/game/entities.h](../include/game/entities.h)
- Implementation: [src/game/entities.c](../src/game/entities.c)
- Map placements parsing: [include/assets/map_loader.h](../include/assets/map_loader.h), [src/assets/map_loader.c](../src/assets/map_loader.c)
- Main-loop integration + event application: [src/main.c](../src/main.c)
- Debug tooling: [src/game/debug_dump.c](../src/game/debug_dump.c)

## Goals and Core Constraints

- **Stable handles**: `EntityId { index, gen }` prevents use-after-free. Index re-use increments the generation.
- **Deterministic**: update, queries, and collision resolution are deterministic given the same inputs.
  - Iteration order is index-order (`0..capacity-1`).
  - Candidate lists from spatial queries are sorted to a deterministic order before resolution.
  - Sorting for sprite rendering is stable.
- **Deferred destruction**: despawns are requested during gameplay but applied via `entity_system_flush()`.
- **Data-driven**: entity definitions come from `Assets/Entities/entities.json`, and maps reference defs by name.

## Concepts

### EntityId

Defined in [include/game/entities.h](../include/game/entities.h).

- `EntityId entity_id_none(void)` returns `{ index=UINT32_MAX, gen=0 }`.
- `bool entity_id_is_none(EntityId id)`.

A live entity is valid only if:
- `id.index < es->capacity`
- `es->alive[id.index] == 1`
- `es->generation[id.index] == id.gen`

### EntityKind

Currently recognized kinds:
- `pickup`
- `projectile`
- `enemy`
- `turret`
- `support`

Only `pickup`, `projectile`, and `enemy` have **kind-specific JSON payload parsing** and runtime behavior today.

### EntityState

`EntityState` exists for enemies and shared bookkeeping:
- `SPAWNING`, `IDLE`, `ENGAGED`, `ATTACK`, `DAMAGED`, `DYING`, `DEAD`

In practice:
- `entity_system_spawn()` initializes entities as `ENTITY_STATE_IDLE`.
- The enemy AI transitions among `IDLE/ENGAGED/ATTACK/DAMAGED/DYING/DEAD`.

## Runtime Data Structures (Public)

### EntityDef / EntityDefs

`EntityDefs` is a dynamically sized array of `EntityDef` loaded from JSON.

`EntityDef` fields:
- `name` (string)
- `sprite` (`EntitySprite`)
- `kind` (`EntityKind`)
- `radius` (float)
- `height` (float)
- `max_hp` (int)
- `u` union:
  - `pickup: EntityDefPickup`
  - `projectile: EntityDefProjectile`
  - `enemy: EntityDefEnemy`

### Entity

`Entity` fields (high level):
- Identity: `id`, `def_id`
- Simulation state: `state`, `state_time`, `hp`, `yaw_deg`
- Physics: `PhysicsBody body` (position, velocity, radius, height, sector, etc.)
- Rendering: `sprite_frame`
- Relationships: `target`, `owner`
- Enemy attack bookkeeping: `attack_has_hit`
- Lifetime: `pending_despawn`

### EntitySystem

Holds pooled entities plus deterministic event + spatial query infrastructure.

Notable internal constraints:
- `capacity` is fixed at `entity_system_init`.
- `events` is preallocated to `capacity` events; events are cleared every tick.
- Spatial hash is rebuilt each tick.

## Public API Reference

All declarations are in [include/game/entities.h](../include/game/entities.h).

### EntityDefs API

- `void entity_defs_init(EntityDefs* defs)`
  - Zero-initializes the container.

- `void entity_defs_destroy(EntityDefs* defs)`
  - Frees the container memory and zeroes it.

- `bool entity_defs_load(EntityDefs* defs, const AssetPaths* paths)`
  - Loads definitions from `Assets/Entities/entities.json`.
  - Returns `false` on load/parse failure; the container is left empty.

- `uint32_t entity_defs_find(const EntityDefs* defs, const char* name)`
  - Returns the def index or `UINT32_MAX` if missing.

### EntitySystem Lifetime

- `void entity_system_init(EntitySystem* es, uint32_t max_entities)`
  - Allocates pools.
  - If `max_entities==0`, defaults to 256.
  - Initializes a free-list, sets generation counters to 1.
  - Initializes spatial hash tables.

- `void entity_system_shutdown(EntitySystem* es)`
  - Frees all allocations.

- `void entity_system_reset(EntitySystem* es, const World* world, const EntityDefs* defs)`
  - Clears all entities for a new level.
  - Preserves capacity but invalidates existing `EntityId`s by incrementing generation for every slot.
  - Sets `es->world` and `es->defs` pointers (not owned).

### Spawning and Resolving Entities

- `bool entity_system_spawn(EntitySystem* es, uint32_t def_index, float x, float y, float yaw_deg, int sector, EntityId* out_id)`
  - Allocates a slot from the free-list.
  - Initializes:
    - `e->def_id = def_index`
    - `e->state = ENTITY_STATE_IDLE`
    - `e->hp = defs[def_index].max_hp`
    - `e->yaw_deg = yaw_deg`
  - Initializes `PhysicsBody` at `(x,y,z)` where `z` is the sector floor (if available).
  - Sets `step_height` based on kind:
    - enemies: `1.0f`
    - others: `0.2f`
  - Sets `body.sector` and `body.last_valid_sector` to `sector`.

- `bool entity_system_resolve(EntitySystem* es, EntityId id, Entity** out)`
  - Validates handle and returns a pointer to the live entity.

- `void entity_system_spawn_map(EntitySystem* es, const MapEntityPlacement* placements, int placement_count)`
  - For each placement, looks up `def_name` via `entity_defs_find` and spawns into the recorded sector.
  - Skips placements with empty `def_name` or invalid sector.

### Despawning

- `void entity_system_request_despawn(EntitySystem* es, EntityId id)`
  - Marks `pending_despawn` and enqueues `id` into `despawn_queue`.
  - Safe to call multiple times.
  - Note: the despawn queue can grow via `realloc` if it fills.

- `void entity_system_flush(EntitySystem* es)`
  - Applies all queued despawns, freeing their slots.
  - Invalidates spatial index.

### Tick, Events, and Collisions

- `void entity_system_tick(EntitySystem* es, const PhysicsBody* player_body, float dt_s)`
  - Clears the event list.
  - **Pass 1**: per-entity state update (movement/AI/projectile motion) in index order.
  - Rebuild spatial index.
  - Enemy-enemy separation using spatial hash (deterministic pair handling).
  - Rebuild spatial index again.
  - **Pass 2**: interactions via spatial queries (pickup touch, projectile damage).

- `const EntityEvent* entity_system_events(const EntitySystem* es, uint32_t* out_count)`
  - Returns a pointer to the event buffer generated by the last tick.

- `bool entity_system_emit_event(EntitySystem* es, EntityEvent ev)`
  - Allows the caller to append events deterministically during effect application.
  - Returns false if the event buffer is full.

- `void entity_system_resolve_player_collisions(EntitySystem* es, PhysicsBody* player_body)`
  - Resolves player-vs-enemy overlap in XY.
  - Runs a bounded number of passes (currently 3), iterating enemies in index order.
  - Uses “block portals” movement rules to prevent near-player portal/sector transitions.

### Queries

- `uint32_t entity_system_query_circle(EntitySystem* es, float x, float y, float radius, EntityId* out_ids, uint32_t out_cap)`
  - Returns entity ids whose centers fall within the query circle.
  - Uses the deterministic spatial hash; rebuilds lazily if needed.

- `uint32_t entity_system_alive_count(const EntitySystem* es)`

### Rendering

- `void entity_system_draw_sprites(const EntitySystem* es, Framebuffer* fb, const World* world, const Camera* cam, int start_sector, TextureRegistry* texreg, const AssetPaths* paths, const float* wall_depth)`
  - Renders billboard sprites with wall occlusion.
  - Requires `wall_depth` from the raycaster for per-column occlusion.
    - `wall_depth[x]` is **portal-aware**: it represents the nearest occluding depth along that screen column after recursing through any portal open spans. Fully open portal boundaries do not occlude sprites, so entities can render across sector boundaries when there is line-of-sight.

Important rendering rules:
- Sprites are sorted back-to-front by depth using a **stable** insertion sort.
- Frame layout: horizontal strip of frames (`frame_x = frame_index * frame_w`, `frame_y = 0`).
- Global transparent color key: `FF00FF` (magenta) is treated as transparent.
- Close-range stability: size clamping is done by clamping the **projection scale** consistently.

## Event Model (Exact)

`EntityEvent` fields:
- `type: EntityEventType`
- `entity: EntityId` (usually the source)
- `other: EntityId` (usually the target)
- `def_id: uint16_t` (def index of the source for tick-generated events)
- `kind: EntityKind` (kind of `def_id`)
- `x,y`: event position (meaning depends on type)
- `amount`: integer payload (meaning depends on type)

Event types and their semantics:

- `ENTITY_EVENT_PLAYER_TOUCH`
  - Emitted for pickups when the player overlaps the pickup trigger.
  - `entity = pickup id`, `def_id = pickup def`, `kind = pickup`.
  - `x,y = pickup position`.

- `ENTITY_EVENT_PROJECTILE_HIT_WALL`
  - Emitted when a projectile’s XY circle movement collides with world geometry.
  - `entity = projectile id`, `def_id = projectile def`, `kind = projectile`.
  - `x,y = projectile position after collision resolution`.

- `ENTITY_EVENT_DAMAGE`
  - Emitted when a projectile overlaps a damageable entity.
  - `entity = source projectile id`
  - `other = target entity id`
  - `def_id/kind = source projectile def`
  - `x,y = target position`.
  - `amount = projectile damage`.
  - Projectile also requests despawn.

- `ENTITY_EVENT_DIED`
  - Not emitted by `entity_system_tick` today.
  - Emitted by the main loop via `entity_system_emit_event` when applying damage reduces a target’s HP to 0.
  - `entity = dead target`, `other = source`, `def_id/kind = target’s def/kind`.

- `ENTITY_EVENT_PLAYER_DAMAGE`
  - Emitted by enemies during the attack state when the windup completes and the player is in range.
  - `entity = enemy id`, `def_id/kind = enemy def`, `amount = attack_damage`.
  - `x,y = player position`.

## Tick Behavior by Kind (Exact)

### Pickups

- No movement/state update in Pass 1.
- In Pass 2:
  - Checks 2D overlap between player and pickup:
    - Radius used: `pickup.trigger_radius + player_body->radius`.
  - Emits `ENTITY_EVENT_PLAYER_TOUCH`.
  - The game loop typically consumes the pickup and calls `entity_system_request_despawn`.

### Projectiles

Pass 1:
- Optional lifetime despawn: if `lifetime_s > 0` and `state_time >= lifetime_s`, request despawn.
- Moves in XY based on `yaw_deg` and `speed`.
- Uses `collision_move_circle` (world collision in XY).
- On collision, emits `ENTITY_EVENT_PROJECTILE_HIT_WALL` and requests despawn.

Pass 2:
- If `damage > 0`, checks overlaps against nearby entities:
  - Queries nearby candidates using the spatial hash.
  - Skips:
    - itself
    - `owner` (if `owner` is set)
    - targets with `max_hp <= 0`
    - pickup/projectile targets
    - targets in different sectors
  - Requires both XY circle overlap and Z overlap of the vertical intervals:
    - projectile: `[z, z+height]`
    - target: `[z, z+height]`
  - Emits `ENTITY_EVENT_DAMAGE` and requests despawn.

### Enemies

Pass 1:
- Faces the player (updates `yaw_deg`).
- State machine:
  - `IDLE`:
    - Updates physics with zero wish velocity.
    - If `dist <= engage_range`, transitions to `ENGAGED`.
  - `ENGAGED`:
    - If `dist > disengage_range`, transitions to `IDLE`.
    - If `dist <= max(attack_range, min_approach)`, transitions to `ATTACK`.
    - Otherwise chases the player by applying a wish velocity toward the player.
    - When near the player, uses a portal-blocking physics update to avoid portal transitions.
    - Enforces minimum player separation by pushing away in XY.
  - `ATTACK`:
    - Updates physics with zero wish velocity.
    - Enforces minimum player separation.
    - At `attack_windup_s`, if player is within range, emits `ENTITY_EVENT_PLAYER_DAMAGE` once.
    - At `attack_cooldown_s`, returns to `ENGAGED`.
  - `DAMAGED`:
    - Holds for `damaged_time_s`, then returns to `ENGAGED` or `IDLE` depending on distance.
  - Death pipeline (driven by `hp <= 0`):
    - Transitions to `DYING`, then `DEAD`, then requests despawn after `dead_time_s`.

Enemy-enemy separation:
- After Pass 1, the system pushes overlapping enemy pairs apart in XY.
- Pairs are resolved deterministically using spatial queries with a sorted candidate list.

### Turret / Support

- JSON parsing recognizes `kind: "turret"` and `kind: "support"`, but there is currently no kind-specific payload parsing and no runtime behavior.
- Such entities may render (if `sprite` is defined) but will not move/attack/pick up/etc.

## Determinism Details

Deterministic behaviors implemented:
- `entity_system_tick` iterates entities in ascending index order.
- Spatial queries use deterministic integer hashing and stamp-based de-duplication.
- Candidate lists from spatial queries are explicitly sorted before resolution.
- Sprite rendering is sorted by depth using a stable insertion sort.

Notes:
- Events are stored in a fixed buffer and dropped if full.
- `entity_system_request_despawn` can `realloc` the despawn queue if it fills; if you want to avoid allocations during tick, size the queue generously (or increase initial cap).

## Data Formats

### Entity Definitions: Assets/Entities/entities.json

File path: [Assets/Entities/entities.json](../Assets/Entities/entities.json)

Root object:

```json
{ "defs": [ /* EntityDef objects */ ] }
```

Each definition object has:

Required:
- `name` (string; unique)
- `kind` (string): `pickup | projectile | enemy | turret | support`

Optional common fields:
- `radius` (float; default `0.35`, clamped to `>= 0.01`)
- `height` (float; default `1.0`, clamped to `>= 0.01`)
- `max_hp` (int; default `0`; must be `> 0` for `enemy`)

Sprite field:
- `sprite` is optional.
- It can be either:
  - Legacy: a string filename
  - Preferred: an object with file + frame metadata

#### `sprite` (legacy string)

```json
"sprite": "shambler.png"
```

Behavior:
- Only sets `EntitySprite.file.name`.
- The renderer will use the loaded texture’s dimensions.
- `sprite.frames.count` defaults to 1.

#### `sprite` (object)

```json
"sprite": {
  "file": {
    "name": "shambler.png",
    "dimensions": {"x": 1280, "y": 256}
  },
  "frames": {
    "count": 10,
    "dimensions": {"x": 128, "y": 256}
  },
  "scale": 1,
  "z_offset": 0
}
```

Required:
- `sprite.file` object
  - `name` string (e.g. `"shambler.png"`)
  - `dimensions` object with positive `x` and `y`
- `sprite.frames` object
  - `count` int, must be `>= 1`
  - `dimensions` object with positive `x` and `y`

Optional:
- `scale` float (defaults to `1.0`; values `<= 0` are forced to `1.0`)
- `z_offset` float (defaults to `0.0`)
  - Meaning: sprite-space pixels above the floor.
  - Conversion: world units are derived using `64px == 1 world unit`, so `ent_z = body.z + z_offset/64`.

Validation rules:
- Frames must fit as a horizontal strip:
  - `frames.dimensions.x * frames.count <= file.dimensions.x`
  - `frames.dimensions.y <= file.dimensions.y`

### Kind-specific payloads

#### Pickup (`kind: "pickup"`)

Requires a `pickup` object.

```json
"pickup": {
  "heal_amount": 25,
  "trigger_radius": 0.6,
  "pickup_sound": "Player_Jump.wav",
  "pickup_sound_gain": 1.0
}
```

Rules (exact):
- You must specify either:
  - `heal_amount`, or
  - both `ammo_type` and `ammo_amount`

Fields:
- `heal_amount` (number; stored as int; no positivity check)
- `ammo_type` (string): `"bullets" | "shells" | "cells"`
- `ammo_amount` (number; stored as int; must be `> 0`)
- `trigger_radius` (float; optional; defaults to `def.radius`; clamped to `>= 0.01`)
- `pickup_sound` (string; optional; WAV under `Assets/Sounds/Effects/`)
- `pickup_sound_gain` (float; optional; default `1.0`)

#### Projectile (`kind: "projectile"`)

Requires a `projectile` object.

```json
"projectile": {
  "speed": 12.0,
  "lifetime_s": 2.0,
  "damage": 10,
  "impact_sound": "Player_Jump.wav",
  "impact_sound_gain": 0.6
}
```

Defaults and clamping:
- `speed`: default `8.0`, clamped to `>= 0.0`
- `lifetime_s`: default `1.0`, clamped to `>= 0.0`
- `damage`: default `10`, must be `>= 0`
- `impact_sound`: default empty string
- `impact_sound_gain`: default `1.0`

#### Enemy (`kind: "enemy"`)

Requires:
- `max_hp > 0`
- an `enemy` object

```json
"enemy": {
  "move_speed": 1.2,
  "engage_range": 6.0,
  "disengage_range": 10.0,
  "attack_range": 1.0,
  "attack_windup_s": 0.25,
  "attack_cooldown_s": 0.9,
  "attack_damage": 10,
  "damaged_time_s": 0.25,
  "dying_time_s": 0.8,
  "dead_time_s": 0.8,
  "animations": {
    "idle": {"start": 0, "count": 2, "fps": 3},
    "engaged": {"start": 2, "count": 2, "fps": 6},
    "attack": {"start": 4, "count": 2, "fps": 8},
    "damaged": {"start": 6, "count": 2, "fps": 8},
    "dying": {"start": 8, "count": 2, "fps": 6},
    "dead": {"start": 9, "count": 1, "fps": 1}
  }
}
```

Defaults and clamping:
- `move_speed`: default `1.2`, clamped to `>= 0.0`
- `engage_range`: default `6.0`, clamped to `>= 0.0`
- `disengage_range`: default `10.0`, clamped to `>= engage_range`
- `attack_range`: default `0.9`, clamped to `>= 0.0`
- `attack_windup_s`: default `0.25`, clamped to `>= 0.0`
- `attack_cooldown_s`: default `0.9`, clamped to `>= attack_windup_s + 0.01`
- `attack_damage`: default `10`, must be `>= 0`
- `damaged_time_s`: default `0.25`, clamped to `>= 0.0`
- `dying_time_s`: default `0.7`, clamped to `>= 0.0`
- `dead_time_s`: default `0.8`, clamped to `>= 0.0`

Animations:
- `enemy.animations` is required and must contain these keys:
  - `idle`, `engaged`, `attack`, `damaged`, `dying`, `dead`
- Each animation object:
  - `start` int (`>= 0`)
  - `count` int (`> 0`)
  - `fps` optional float (defaults to `6.0`; values `<= 0` are forced to `6.0`)
- Validation: `start + count` must be within `sprite.frames.count`.

## Map-authored entity placements (Levels/*.json)

Maps can contain an optional root field `entities`, parsed by [src/assets/map_loader.c](../src/assets/map_loader.c) into `MapLoadResult.entities`.

Format:

```json
"entities": [
  {"def": "health_pickup", "x": 3.5, "y": 2.0, "yaw_deg": 0.0},
  {"def": "shambler", "x": 10.0, "y": 7.0, "yaw_deg": 90.0}
]
```

Rules (exact):
- `entities` must be an array if present.
- Each entry must be an object with required:
  - `x` (number)
  - `y` (number)
- Optional:
  - `yaw_deg` (number; defaults to `0.0`)
  - `def` (string) — preferred
  - `type` (string) — legacy

Legacy `type` mapping (currently implemented):
- `"pickup_health"` -> `"health_pickup"`

Sector assignment:
- The loader computes `sector = world_find_sector_at_point(world, x, y)`.
- If the point is not inside any sector, map load fails.

Missing/unknown defs:
- If `def` is missing and `type` is unknown/missing, the placement is kept but marked inactive:
  - `def_name` becomes empty
  - `sector` becomes `-1`
- `entity_system_spawn_map` will skip inactive placements.

## Main loop integration (current pattern)

The main fixed-step loop in [src/main.c](../src/main.c) integrates entities like this:

1. Player movement/controller update.
2. `entity_system_resolve_player_collisions(&entities, &player.body)`.
3. `entity_system_tick(&entities, &player.body, dt)`.
4. Apply effects by consuming events:
   - Pickups: modify player health/ammo, play pickup SFX, request pickup despawn.
   - Projectile wall hits: play impact SFX, request despawn.
   - Projectile damage: apply target HP change; on kill emit `ENTITY_EVENT_DIED` via `entity_system_emit_event`; set enemy to `DYING` or despawn non-enemy targets.
   - Enemy melee: apply `ENTITY_EVENT_PLAYER_DAMAGE` to the player.
5. `entity_system_flush(&entities)`.

Important detail: the event processing loop in main uses an index (`ei`) and repeatedly re-reads `event_count` so that events emitted during effect application (e.g. `DIED`) are processed in the same frame.

## Weapon integration (current)

In [src/game/weapons.c](../src/game/weapons.c), the handgun spawns a simple projectile entity:
- Looks up `"test_projectile"` via `entity_defs_find`.
- Spawns slightly in front of the player to avoid immediate overlap.
- `owner` is currently left as none because the player is not yet an entity.

## Debugging

- Entity dump: press the configured `entity_dump` key (default `L`) to print a full entity dump, including sprite projection diagnostics. See [src/game/debug_dump.c](../src/game/debug_dump.c).
- Debug dump: run with `--debug-dump` and press the configured `debug_dump` key to print world/raycast info.

When debugging close-range sprite behavior:
- The entity dump prints `depth`, `proj_depth`, and `scale` (px/world-unit) using the same clamping rules as the renderer.