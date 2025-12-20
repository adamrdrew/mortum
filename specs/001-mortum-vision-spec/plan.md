# Implementation Plan: Rendering, HUD, and Lighting Improvements

**Branch**: `001-mortum-vision-spec` | **Date**: 2025-12-19 | **Spec**: `specs/001-mortum-vision-spec/spec.md`
**Input**: Feature specification from `/specs/001-mortum-vision-spec/spec.md` + follow-up improvements request (depth occlusion, Doom-style HUD, lighting).

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

Improve moment-to-moment readability and “Doom-like” feel by fixing sprite depth ordering, replacing the debug-text HUD with a Doom-style bottom status bar, and adding a simple lighting model (distance fog/shading + sector light tint + optional point lights).

## Technical Context

<!--
  ACTION REQUIRED: Replace the content in this section with the technical details
  for the project. The structure here is presented in advisory capacity to guide
  the iteration process.
-->

**Language/Version**: C11 (clang primary, gcc secondary)  
**Primary Dependencies**: SDL2 (window/input/audio)  
**Storage**: JSON files in `Assets/` (maps, episodes)  
**Testing**: `make test` (C tests; asset validation via `make validate`)  
**Target Platform**: macOS, Linux, Windows (MinGW)  
**Project Type**: single C game executable (software framebuffer renderer)  
**Performance Goals**: 60 FPS at default internal resolution; no per-frame heap churn in renderer  
**Constraints**: minimal deps; renderer remains readable C; preserve gameplay↔platform boundaries  
**Scale/Scope**: incremental improvements to existing raycaster + HUD; no new editor tooling in this slice

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Source of truth: `.specify/memory/constitution.md`

- Readability: simplest correct approach; avoid clever tricks.
- Safety: no undefined behavior knowingly; warnings treated seriously.
- Ownership: every allocation/resource has an owner; init/destroy is explicit.
- Boundaries: gameplay ↔ platform separation; isolate `#ifdef` usage.
- Builds/docs: keep `make` workflow boring; avoid painful dependencies.

GATE STATUS: PASS (plan uses small, local changes; no new heavy deps; keeps rendering in `render/` and avoids platform leakage).

## Project Structure

### Documentation (this feature)

```text
specs/[###-feature]/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)
<!--
  ACTION REQUIRED: Replace the placeholder tree below with the concrete layout
  for this feature. Delete unused options and expand the chosen structure with
  real paths (e.g., apps/admin, packages/something). The delivered plan must
  not include Option labels.
-->

```text
src/
  main.c
  render/
    raycast.c
    entities.c
    draw.c
    font.c
    framebuffer.c
  game/
    hud.c
  assets/
    map_loader.c

include/
  render/
    raycast.h
    entities.h
  game/
    world.h
    hud.h

Assets/
  Levels/
  Images/
  Episodes/
```

**Structure Decision**: Single C project. Rendering stays inside `src/render/*` and consumes `game/world.h` + lightweight inputs; HUD stays in `src/game/hud.c` and uses `render/font.h` + `render/draw.h`.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| *(none)* |  |  |

## Phase 0 — Research (resolve open questions)

1. Confirm root cause of “depth occlusion wrong”:
  - Likely sprite-vs-sprite ordering (entities currently drawn in list order), not wall occlusion.
  - Verify depth values are perpendicular-correct and in same space as sprite `perp`.
2. Choose simplest sprite occlusion fix:
  - Sort visible sprites by `perp` descending (far→near) before drawing.
  - Keep wall depth buffer test as-is.
3. Define lighting model that is readable and cheap:
  - Base distance falloff (already exists for walls).
  - Multiply by sector light intensity + tint.
  - Optional point lights (small list) applied as an additive factor at sample point.

## Phase 1 — Design (data model + contracts)

1. Data model changes:
  - Extend `Sector` to support light color (tint) in addition to intensity.
  - Define map JSON representation for `sector.light_color` and optional `lights[]`.
2. Contracts:
  - Add a schema extension for map lighting fields.
3. UX/HUD design:
  - Replace debug text HUD with a Doom-style bottom bar (labeled HP/MORTUM/AMMO/KEYS).
  - Include simple bevel/contrast rectangles to imply lighting.

## Phase 2 — Implementation Plan (high-level steps)

1. Depth occlusion correctness
  - Build a temporary array of visible billboard candidates.
  - Sort by `perp` descending and draw in that order.
  - Optional follow-up: per-column sprite depth buffer to improve sprite-vs-sprite overlaps without full per-pixel z.
2. Doom-style HUD
  - Reserve a bottom status area (fixed pixel height).
  - Draw background + border highlights; render labeled numbers.
  - Ensure it remains readable at different framebuffer sizes.
3. Lighting
  - Replace current distance-only shading with `apply_lighting(color, dist, sector_light, sector_tint, point_lights)`.
  - Apply lighting to walls and billboards (enemies + pickups) consistently.
  - Add sector tint + intensity to map format; default to existing `light` with white tint.

STOP CONDITION: This plan phase ends here; implementation is executed in follow-up work.

## Constitution Check (Post-Design Re-check)

- Still PASS: proposed changes stay in existing modules, avoid new dependencies, and keep rendering logic readable and explicit.
