# PhysicsBody (Kinematic Physics)

This document describes the engine’s current kinematic physics system implemented as `PhysicsBody`.

Goals:

- Make movement + collision reusable for the player *and* future entities (enemies, pickups with physics, etc).
- Support uneven floor traversal (sector-to-sector `floor_z` changes):
  - Stepping **up** is allowed only for small height deltas (currently one world unit).
  - Stepping **down** triggers **falling** with gravity (no snapping).
- Never allow falling acceleration to clip below floors.
- Keep physics decoupled from rendering; physics should not depend on renderer internals.

## High-level model

A `PhysicsBody` is a **capsule-like** approximation:

- 2D collision shape: **circle** with radius `radius` on the X/Y plane.
- Vertical extent: body has a **feet height** `z` and a **height** `height`.

Key fields (see `include/game/physics_body.h`):

- `x, y`: horizontal position.
- `z`: **feet** (bottom) height in world units.
- `vx, vy`: horizontal velocity (measured post-solve each update).
- `vz`: vertical velocity used for falling.
- `radius`: circle collision radius in X/Y.
- `height`: used for headroom checks against sector ceilings.
- `step_height`: maximum permitted floor step-up delta.

The body also tracks its sector:

- `sector`: current sector index (best-effort).
- `last_valid_sector`: last known valid sector index, used for stability.

## World interaction assumptions

Maps already store per-sector vertical bounds:

- `Sector.floor_z`
- `Sector.ceil_z`

Note: `Sector.floor_z` can be modified at runtime by gameplay systems (e.g. movable floors / sector height manipulation). `PhysicsBody` always queries the current values each tick.

Walls define adjacency:

- `Wall.back_sector == -1` → solid wall.
- otherwise → portal between `front_sector` and `back_sector`.

Physics reuses the existing wall winding assumption used by rendering:

- A wall’s directed edge `(v0 -> v1)` defines which side is `front_sector` vs `back_sector`.

Sector membership queries are implemented in `src/game/world.c`:

- `world_find_sector_at_point()`
- `world_find_sector_at_point_stable()`

## Update loop (what to call)

Main entry point:

- `physics_body_update(PhysicsBody* b, const World* world, float wish_vx, float wish_vy, double dt_s, const PhysicsBodyParams* params)`

This:

1. Resolves **horizontal** movement in X/Y with collision.
2. Handles portal traversal rules (step-up vs block).
3. Applies **vertical** simulation:
   - step-down → falling under gravity
   - hard clamping to prevent going below floors
4. Updates `sector` tracking and `on_ground`.

There is also a one-off movement helper:

- `physics_body_move_delta(...)`

Used for “impulse-like” moves (e.g. scripted pushes), so these use the *same* traversal rules.

## Collision rules

### Solid walls

If `wall.back_sector == -1`, the wall is always colliding.

Resolution is an iterative “push-out + slide” solver against wall segments using the body’s 2D circle.

### Portal walls (two-sided)

If a wall has a valid `back_sector`, it is *potentially* passable. Whether it blocks depends on the height relationship of the two sectors.

A portal blocks if:

- The target sector is too high to step into:

  $$\Delta = \text{to.floor\_z} - \text{from.floor\_z} > \text{body.step\_height}$$

- Or the body wouldn’t fit under the destination ceiling at the destination floor:

  $$\text{to.floor\_z} + \text{body.height} > \text{to.ceil\_z} - \varepsilon$$

If neither is true, the portal is treated as open for collision purposes.

## Stepping and falling

### Step-down (higher → lower)

When the body crosses into a sector with a lower `floor_z`:

- The body becomes airborne (if it was effectively on the old floor).
- Gravity accelerates `vz` down.
- `z` is clamped to the new floor to guarantee no under-floor penetration.

This produces a smooth fall and avoids “teleport to the lower plane”.

### Step-up (lower → higher)

When the body attempts to cross into a sector with a higher `floor_z`, step-up is allowed only if:

- `delta_floor_z > 0` and `delta_floor_z <= step_height`
- Destination headroom fits (`height` under `ceil_z`)

#### Animation and input feel

Step-up is intentionally:

- **Fast** (default ~0.08s)
- **Atomic / non-interruptible** (the body is in a `step_up.active` state)

To avoid the feeling of “snapping” *and* avoid the feeling of input freezing, step-up works like this:

1. Detect that the resolved horizontal movement would enter a higher-floor sector.
2. Start step-up animation:
   - Animate `z` from current value to the destination `floor_z`.
3. During the animation:
   - Apply the step’s horizontal movement **progressively** so forward momentum feels preserved.
   - Keep `sector` locked to the origin sector until completion (prevents render-side discontinuity).
4. On completion:
   - Switch the body’s `sector` to the destination sector.

This yields:

- Visually continuous stepping (no instant camera-floor snap).
- Controls feel responsive (movement continues through the step).

## Safety: “no clipping under any circumstances”

The system enforces floor safety in multiple ways:

- Gravity is applied only when not `on_ground`.
- After vertical integration, `z` is **hard-clamped**:

  - if `z < floor_z` → set `z = floor_z`, `vz = 0`, `on_ground = true`

This guarantees the body never ends a tick below its current sector floor.

## Parameters and tuning

`PhysicsBodyParams` provides global-ish knobs:

- `gravity_z`: negative acceleration.
- `step_duration_s`: how fast step-up animates.
- `floor_epsilon`, `headroom_epsilon`: tolerances.
- `max_substep_dist`: limits horizontal tunneling by subdividing large moves.
- `max_solve_iterations`: collision solver iterations per substep.

Player defaults are set in `src/game/player.c` via:

- `physics_body_init(&p->body, ..., radius=0.20f, height=1.75f, step_height=1.00f)`

## Adding a new entity (enemy) using PhysicsBody

Recommended pattern:

1. Add a new entity struct that *contains* a `PhysicsBody`:

   - `PhysicsBody body;`

2. On spawn/init, call:

   - `physics_body_init(&enemy->body, x, y, z, radius, height, step_height);`

3. On update tick, compute desired velocity (AI steering):

   - `physics_body_update(&enemy->body, world, wish_vx, wish_vy, dt_s, &params);`

4. Render/aim logic should read:

- `enemy->body.x/y` for position.
- `enemy->body.z` for feet height if needed.

Notes:

- If different enemy sizes exist, set their `radius` and `height` accordingly.
- If an enemy should not step up ledges (e.g. crawling), set `step_height` lower.

## Current limitations (intentional)

- No jumping.
- No slopes (floors are per-sector planes).
- Collision is still 2D against wall segments; vertical constraints come from sector floor/ceil values.

These are consistent with the current map representation and renderer.
