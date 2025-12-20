# Research — Texture Floors/Ceilings + New Texture Library

This document resolves the open questions from `plan.md` Phase 0.

## Decision 1: Support PNG textures

**Decision**: Add PNG decoding support for textures referenced by maps (files in `Assets/Images/Textures/`).

**Rationale**:
- The new texture library currently consists of `.PNG` files.
- Current code path `render/texture.c` → `assets/image_load_bmp()` only supports BMP, so without PNG decoding most textures cannot load.

**Alternatives considered**:
- Use SDL_image: rejected because it adds a system dependency and complicates the “boring build” goal.
- Preconvert textures to BMP at build time: rejected because it adds asset-pipeline complexity and makes iteration slower.

## Decision 2: Texture search path

**Decision**: Default to loading textures from `Assets/Images/Textures/<filename>`. Optionally keep a fallback to `Assets/Images/<filename>` for backwards compatibility.

**Rationale**:
- Matches the updated asset layout described in the feature request.
- Fallback keeps older maps (and any remaining BMP assets) working without requiring immediate migration.

**Alternatives considered**:
- Require maps to include subpaths like `Textures/BRICK_1A.PNG`: rejected because it bakes directory structure into map data and is harder to change later.

## Decision 3: Floor/ceiling rendering approach

**Decision**: Implement textured floors and ceilings in the raycaster using a classic “floor casting” approach, choosing the sector’s `floor_tex`/`ceil_tex` based on the ray hit (front sector of the closest wall).

**Rationale**:
- Keeps the implementation in one place (`src/render/raycast.c`) and aligned with the current rendering model.
- Produces a visible improvement without requiring a full multi-height portal renderer.

**Alternatives considered**:
- “Just texture-fill the bottom half/top half” with one texture: rejected because it does not meet the per-sector requirement.
- Implement full Doom-style visplanes/portals with varying floor/ceil heights: rejected as out-of-scope complexity for this feature.

## Decision 4: Texture size assumptions

**Decision**: Treat textures as 64x64 for sampling. If an image loads with a different size, log an error and fall back to the debug/magenta color path.

**Rationale**:
- The library is defined to be 64x64; enforcing simplifies sampling math and avoids weird stretching.

**Alternatives considered**:
- Support arbitrary sizes: rejected for now (added complexity) and not needed for current assets.

## Example map texture palette

To keep map updates consistent and easy to eyeball, start by using a small palette:

- Walls: `BRICK_3A.PNG`, `CONCRETE_2A.PNG`, `STEEL_2A.PNG`
- Floors: `FLOOR_2A.PNG`, `GRID_1A.PNG`
- Ceilings: `TECH_1A.PNG`, `PANEL_2A.PNG`
