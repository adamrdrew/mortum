# Mortum Architecture (Early)

## Module boundaries

- `src/platform/`: SDL2-only code (window, input, time, audio, filesystem). Gameplay must not call SDL directly.
- `src/render/`: software renderer + framebuffer; consumes world/camera data.
- `src/game/`: world model, entities, rules; must not call SDL directly.
- `src/assets/`: loading/parsing of timelines/maps/images/sounds.

Notable cross-cutting helpers:

- `src/game/level_start.c`: level-start setup and per-level player resets.
- `src/game/debug_overlay.c`: optional debug text overlay.
- `src/game/console.c` + `src/game/console_commands.c`: in-game Quake-style console and developer commands.
- `src/game/debug_spawn.c`: dev-only spawn helpers.

Public headers live in `include/` mirroring the module names.

## Ownership conventions

- Every module that allocates memory provides an explicit `*_init` / `*_destroy` pair.
- Heap allocations have a clear owner; borrowed pointers are only valid for the duration of the call unless explicitly documented.
- Resources (SDL window, renderer, textures, audio buffers) follow explicit init/destroy.

## Main loop

- Fixed timestep update (60Hz target) with an accumulator.
- Render as fast as possible; present a CPU framebuffer scaled to the window.

## Textures

- Textures are loaded through the `TextureRegistry` (`src/render/texture.c`).
- Lookup is by filename only; preferred location is `Assets/Images/Textures/<filename>` with a backward-compatible fallback to `Assets/Images/<filename>`.
- Supported formats are PNG and BMP. PNG decoding is dependency-free via vendored LodePNG (`third_party/lodepng.*`) and is converted to the engine’s ABGR8888 pixel format.
- Map sectors provide `floor_tex` and `ceil_tex` and the raycaster (`src/render/raycast.c`) draws textured floors/ceilings per sector.
- Current PNG map textures are expected to be 64x64; invalid sizes are rejected with a clear log error.

## Entity definitions

- Entity definitions live in `Assets/Entities/entities.json` and are loaded by the gameplay module (`src/game/entities.c`).
- Each entry in `defs[]` has common fields:
	- `name` (string, unique)
	- `kind` (string: `pickup`, `projectile`, ...)
	- `radius` / `height` (numbers; collision bounds)
	- `max_hp` (optional int; if > 0 the entity can receive damage)

Enemies and the player share the same underlying `PhysicsBody` traversal system (step-up, portals, gravity/falling).

### Sprite schema

- `sprite` can be either:
	- a legacy string filename (e.g. `"health_pickup.png"`), or
	- an object with explicit metadata:
		- `file.name` (string)
		- `file.dimensions.x/y` (ints)
		- `frames.count` (int >= 1) and `frames.dimensions.x/y` (ints)
		- `scale` (number, default 1)
		- `z_offset` (number, in sprite pixels above the floor; converted using 64px == 1 world unit)
- Sprite sheets are currently treated as a horizontal strip of `frames.count` frames.
- Sprite rendering treats the color key `FF00FF` (magenta) as transparent globally (in addition to alpha=0).

### Spatial queries

- `EntitySystem` maintains a deterministic spatial hash index rebuilt each tick.
- `EntitySystem` maintains a deterministic spatial hash index rebuilt each tick.
- `entity_system_query_circle(...)` returns nearby entity ids for fast proximity queries (used by projectiles and intended for AI).
- Enemies also run a deterministic enemy–enemy separation pass to avoid interpenetration.

## Tools

- `make validate` builds and runs an offline asset loader/validator (timelines + maps).
