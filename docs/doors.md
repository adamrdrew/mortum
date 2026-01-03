# Doors (Developer Documentation)

Mortum’s door system is a **first-class, map-authored gameplay primitive** that binds to an existing **portal wall** and can make that portal behave like a **solid wall while “closed.”**

Core constraints:

- Doors are **open-only**: once opened, they never close in gameplay.
- Door “closure” is represented without changing world topology: the wall remains a portal (`back_sector != -1`) but is treated as solid via a runtime flag.
- Doors visually **animate open by rising upward through the ceiling** (fast, readable lift), rather than disappearing.

---

## Source of truth

- Public API + types: [include/game/doors.h](../include/game/doors.h)
- Implementation: [src/game/doors.c](../src/game/doors.c)
- Map schema (loader structs): [include/assets/map_loader.h](../include/assets/map_loader.h)
- Map parsing: [src/assets/map_loader.c](../src/assets/map_loader.c)
- Map validation (including door rules): [include/assets/map_validate.h](../include/assets/map_validate.h), [src/assets/map_validate.c](../src/assets/map_validate.c)
- Runtime wall flag used by doors: [include/game/world.h](../include/game/world.h)

Engine integrations (door-closed behaves solid):

- Collision + LOS: [src/game/collision.c](../src/game/collision.c)
- Physics portal traversal: [src/game/physics_body.c](../src/game/physics_body.c)
- Portal raycasting recursion: [src/render/raycast.c](../src/render/raycast.c)

User interaction wiring:

- Action-key interaction ordering and build/teardown: [src/main.c](../src/main.c)
- Timeline transitions rebuild/teardown: [src/game/timeline_flow.c](../src/game/timeline_flow.c)
- Console commands: [src/game/console_commands.c](../src/game/console_commands.c)

---

## Design: “blocked portal” (no topology mutation)

A door is authored on a wall that is already a portal (`Wall.back_sector != -1`).

- When the door is **closed**, the engine sets:
  - `Wall.door_blocked = true`
  - `Wall.door_open_t = 0.0f`
  - `Wall.tex = Door.closed_tex` (the map-authored door slab texture)
- When the door is **opened**, the engine sets:
  - during the opening animation: `Wall.door_blocked` remains `true` while `Wall.door_open_t` ramps from `0 → 1`
  - at the end of the animation: `Wall.door_blocked = false`, `Wall.door_open_t = 1.0f`, and `Wall.tex = Wall.base_tex` (restores the authored wall texture)

Important: portal edges are typically represented by **two directed walls** (one per sector). The door system applies the blocked flag and texture swap to both the bound wall and its “twin” (reversed vertices + swapped sectors) when present. This avoids one-sided rendering where the raycaster hits the opposite-directed wall and draws an “open portal span” (fully see-through when floor/ceil match).

This preserves renderer assumptions about portal graphs and sector-wall indices (the wall remains a portal wall in data), while still allowing gameplay systems to treat it as solid.

---

## Map format: `doors[]`

Doors are authored in the level JSON as a top-level optional array:

```json
{
  "doors": [
    {
      "id": "lab_door",
      "wall_index": 17,
      "tex": "DOOR_LAB.PNG",
      "starts_closed": true,
      "sound_open": "Door_Open.wav",
      "required_item": "green_key",
      "required_item_missing_message": "The door is locked."
    }
  ]
}
```

### Fields (`MapDoor`)

Declared in [include/assets/map_loader.h](../include/assets/map_loader.h).

Required:

- `id` (string, non-empty, max 63 bytes)
- `wall_index` (integer): index into `World.walls[]`
- `tex` (string, non-empty, max 63 bytes): the wall texture to use while the door is closed

Optional:

- `starts_closed` (boolean, default `true`)
- `sound_open` (string, optional WAV under `Assets/Sounds/Effects/`, max 63 bytes)
- `required_item` (string, optional inventory item id, max 63 bytes)
- `required_item_missing_message` (string, optional toast message, max 127 bytes)

### Validation rules

Enforced by `map_validate(...)` in [src/assets/map_validate.c](../src/assets/map_validate.c):

- Door IDs must be unique within the map.
- `wall_index` must be in range.
- The referenced wall must be a **portal wall** (`back_sector != -1`).
- The referenced wall must not have `end_level=true` (end-level takes precedence over door interaction).
- `tex` must be non-empty.

---

## Runtime data model

### `Wall.door_blocked`

Declared in [include/game/world.h](../include/game/world.h):

- `bool door_blocked;`

Meaning:

- If `door_blocked` is true, this wall behaves like a **solid wall** for:
  - collision and line-of-sight
  - physics portal traversal
  - renderer portal recursion

The wall still remains a portal in topology (it still has a `back_sector`), which is important for renderer/visibility structure.

### `Wall.door_open_t`

Declared in [include/game/world.h](../include/game/world.h):

- `float door_open_t;`

Meaning:

- Door open animation fraction in `[0,1]`.
  - `0.0` = fully closed (door slab spans the full open height)
  - `1.0` = fully open (door has risen through the ceiling)
- While `door_blocked == true` and `door_open_t > 0`, the **renderer** draws the door as a slab that is lifted upward, leaving an **increasing portal gap below**.
- Collision/LOS/physics continue to treat the portal as solid as long as `door_blocked == true` (door remains blocking until the animation completes).

### Door runtime structs

Declared in [include/game/doors.h](../include/game/doors.h):

- `Door` — per-door runtime state (id, bound wall, open state, gating fields, cooldown timestamps)
- `Doors` — owning container for `Door[]`

Ownership:

- `Doors.doors` is heap-allocated and owned by `Doors`.
- `doors_destroy()` frees it.

---

## Public API surface

All of the following are declared in [include/game/doors.h](../include/game/doors.h).

### Result codes

```c
typedef enum DoorsOpenResult {
	DOORS_OPENED = 0,
	DOORS_ALREADY_OPEN = 1,
	DOORS_NOT_FOUND = 2,
	DOORS_ON_COOLDOWN = 3,
	DOORS_MISSING_REQUIRED_ITEM = 4,
	DOORS_INVALID = 5,
} DoorsOpenResult;
```

Meaning:

- `DOORS_OPENED`: door opening was accepted/initiated (may animate before the portal becomes passable)
- `DOORS_ALREADY_OPEN`: target door is already open
- `DOORS_NOT_FOUND`: requested ID not present
- `DOORS_ON_COOLDOWN`: open attempt ignored due to debounce
- `DOORS_MISSING_REQUIRED_ITEM`: inventory gating prevented opening (toast may be emitted)
- `DOORS_INVALID`: bad args or invalid door binding

### Lifetime

- `void doors_init(Doors* self)`
  - Initializes to an empty container.

- `void doors_destroy(Doors* self)`
  - Frees owned door storage and resets to empty.

### Build from map

- `bool doors_build_from_map(Doors* self, World* world, const MapDoor* defs, int def_count)`
  - Clears any existing runtime doors and rebuilds from `MapDoor` definitions.
  - Applies initial closed/open state to bound walls:
    - for `starts_closed=true`: sets `wall.door_blocked=true` and swaps to the closed texture
    - for `starts_closed=false`: ensures `wall.door_blocked=false`

Notes:

- This call is expected to happen after `map_load()` succeeds and before gameplay begins.
- This function mutates `world->walls[wall_index]` for each door.

### Introspection

- `int doors_count(const Doors* self)`
- `const char* doors_id_at(const Doors* self, int index)`

These are used by the console `door_list` command.

### Gameplay interaction

- `bool doors_try_open_near_player(Doors* self, World* world, const Player* player, Notifications* notifications, SoundEmitters* sfx, float listener_x, float listener_y, float now_s)`

Behavior:

- Finds the nearest **closed** door within interaction radius of the player.
- Requires the player to be in one of the door wall’s adjacent sectors (front or back).
- On success, opens the door and returns `true` only for an actual closed→open transition.

- `DoorsOpenResult doors_try_open_by_id(Doors* self, World* world, const Player* player, Notifications* notifications, SoundEmitters* sfx, float listener_x, float listener_y, float now_s, const char* door_id)`

Behavior:

- Opens a door by ID.
- Uses the same gating/cooldown/toast behavior as a nearby interaction.
- Does not apply proximity/sector-adjacency checks (intended for developer tooling/console).

### Per-tick animation update

- `void doors_update(Doors* self, World* world, float now_s)`

Behavior:

- Advances opening animations for any doors currently opening.
- Updates the bound wall(s) `door_open_t` while keeping `door_blocked = true`.
- When the animation completes, clears `door_blocked` and restores `base_tex`.

Integration requirement:

- Call this once per gameplay tick using the same deterministic gameplay time base as the interaction calls (see [src/main.c](../src/main.c)).

---

## Interaction rules and tuning constants

These are implemented in [src/game/doors.c](../src/game/doors.c).

- Interaction radius: `1.0` world units (player distance to closest point on wall segment)
- “Missing required item” toast cooldown: `0.75s` per door
- Successful open debounce: `15.0s` per door
- Door open animation duration: `DOOR_OPEN_DURATION_S` in [src/game/doors.c](../src/game/doors.c)

Time base:

- The door system expects a deterministic `now_s` (gameplay time seconds) passed in by the caller.
- Main uses a fixed-step gameplay clock rather than wall-clock time.

### Inventory gating

If `Door.required_item` is non-empty, opening requires:

- `inventory_contains(&player->inventory, required_item)`

If missing:

- a toast notification is emitted (if `Notifications*` is provided and the per-door deny-toast cooldown has elapsed)
- message selection:
  - use `required_item_missing_message` if non-empty
  - else fall back to `"Missing required item: <required_item>"`

### Audio

If `sound_open` is non-empty and `SoundEmitters* sfx` is provided:

- plays a one-shot sound at the wall midpoint via `sound_emitters_play_one_shot_at(...)`

---

## How doors affect the engine

### Collision / LOS

When a door is closed (`wall.door_blocked == true`), the portal wall is treated as solid by:

- `collision_move_circle(...)`
- `collision_line_of_sight(...)`

### Physics portal traversal

When closed, the physics body will not traverse the portal between sectors.

### Rendering

When closed, the raycaster treats the wall as an occluder and does not recurse through it as a portal.

While opening (animated):

- The raycaster renders a **door slab** that rises upward, exposing an **open portal gap below**, using `Wall.door_open_t`.
- Collision/LOS/physics remain blocked until the opening animation completes.

---

## Extending the door system

Common extensions and where to implement them:

- Add “close” support:
  - Add a `doors_try_close_*` API and teach engine systems to respect a closed state (they already do via `door_blocked`).
  - Decide how to restore textures and whether to play different SFX.
- Change animation timing/feel:
  - Tune `DOOR_OPEN_DURATION_S` in [src/game/doors.c](../src/game/doors.c).
  - If you change easing, keep the `[0,1]` semantics of `Wall.door_open_t` consistent.
- Add per-door interaction distance or sound gain:
  - Extend `MapDoor` and parsing/validation; store in `Door`; use in `doors_try_open_*`.

When extending map schema:

- Update parsing in [src/assets/map_loader.c](../src/assets/map_loader.c)
- Update validation in [src/assets/map_validate.c](../src/assets/map_validate.c)
- Update authoring docs in [docs/map.md](map.md)
