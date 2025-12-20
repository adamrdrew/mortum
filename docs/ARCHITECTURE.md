# Mortum Architecture (Early)

## Module boundaries

- `src/platform/`: SDL2-only code (window, input, time, audio, filesystem). Gameplay must not call SDL directly.
- `src/render/`: software renderer + framebuffer; consumes world/camera data.
- `src/game/`: world model, entities, rules; must not call SDL directly.
- `src/assets/`: loading/parsing of episodes/maps/images/sounds.

Notable cross-cutting helpers:

- `src/game/episode_runner.c`: episode progression (map index + carry-forward state).
- `src/game/debug_overlay.c`: optional debug text overlay.
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

## Tools

- `make validate` builds and runs an offline asset loader/validator (episode + maps).
