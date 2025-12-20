# Quickstart — Texture Floors/Ceilings + New Texture Library

## Prereqs

- C compiler (macOS: Xcode Command Line Tools)
- SDL2 development libraries (macOS: `brew install sdl2`)

## Build

- `make clean && make`

## Run

- `make run`
- Run a specific map: `make run RUN_MAP=arena.json`

## Verify this feature

1. Confirm textures exist under `Assets/Images/Textures/`.
2. Load an example map (e.g. `arena.json`).
3. Verify:
   - Walls render with textures from the new library.
   - Floors and ceilings are textured (not flat colors).
   - No missing-texture errors appear in logs.

## Mapper notes

- Maps reference textures by filename only (e.g. `BRICK_1A.PNG`).
- Per-sector floor/ceiling textures are specified via `floor_tex` and `ceil_tex` in each sector.
