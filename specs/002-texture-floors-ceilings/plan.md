# Implementation Plan: Texture Floors/Ceilings + New Texture Library

**Branch**: `002-texture-floors-ceilings` | **Date**: 2025-12-20 | **Spec**: `specs/002-texture-floors-ceilings/spec.md`
**Input**: Feature specification from `specs/002-texture-floors-ceilings/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

Upgrade texture handling so maps can reference the expanded texture library under `Assets/Images/Textures/` (64x64), update the example maps to use those textures, and render textured floors/ceilings based on each sector’s `floor_tex`/`ceil_tex`.

## Technical Context

<!--
  ACTION REQUIRED: Replace the content in this section with the technical details
  for the project. The structure here is presented in advisory capacity to guide
  the iteration process.
-->

**Language/Version**: C11  
**Primary Dependencies**: SDL2 (window/input/audio), vendored `jsmn` via `assets/json.c`  
**Storage**: JSON asset files under `Assets/` (maps, episodes, images, sounds)  
**Testing**: `make test` (C test harness)  
**Target Platform**: macOS, Linux, Windows (SDL2)  
**Project Type**: single native C project (software renderer)  
**Performance Goals**: 60 fps at current default resolution on typical dev machines  
**Constraints**: keep build simple (`make`), avoid heavy dependencies; keep rendering changes readable  
**Scale/Scope**: limited scope: texture loader path + image format support + raycast floor/ceiling texturing + example maps

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Source of truth: `.specify/memory/constitution.md`

- Readability: simplest correct approach; avoid clever tricks.
- Safety: no undefined behavior knowingly; warnings treated seriously.
- Ownership: every allocation/resource has an owner; init/destroy is explicit.
- Boundaries: gameplay ↔ platform separation; isolate `#ifdef` usage.
- Builds/docs: keep `make` workflow boring; avoid painful dependencies.

GATE STATUS (pre-research): PASS (approach is small, readable, and keeps dependencies minimal).

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
  assets/
    image_*.c
    map_loader.c
  game/
    world.c
  render/
    raycast.c
    texture.c

Assets/
  Images/
    Textures/   # new texture library (64x64)
  Levels/       # example maps to update
```

**Structure Decision**: Single native C project; all changes are in existing modules under `src/assets/`, `src/render/`, and asset JSON under `Assets/Levels/`.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| [e.g., 4th project] | [current need] | [why 3 projects insufficient] |
| [e.g., Repository pattern] | [specific problem] | [why direct DB access insufficient] |

## Phase 0 — Research (output: `research.md`)

Decisions to finalize before implementation:

1. Texture file format support (`.PNG` present in `Assets/Images/Textures/` while current loader is BMP-only).
2. Texture search path (new default: `Assets/Images/Textures/`, with optional backwards-compatible fallback to `Assets/Images/`).
3. Floor/ceiling rendering approach (implement classic floor casting in the raycaster; keep code readable and bounded).

## Phase 1 — Design (outputs: `data-model.md`, `contracts/`, `quickstart.md`)

- Confirm map v1 contract: `Sector.floor_tex` and `Sector.ceil_tex` are required and are consumed by the renderer.
- Define texture resolution assumptions: map textures are 64x64; non-64x64 is rejected or handled explicitly.
- Document loader responsibilities and error behavior (log + visible fallback).

Re-check Constitution (post-design): still PASS (minimal dependency, readable raycast changes, explicit ownership).

## Phase 2 — Implementation Plan (planning only)

Planned code changes (high level):

- `render/texture.c`: load from `Images/Textures/` (and optionally fallback to `Images/`); add PNG load support in `assets/image.*`.
- `render/raycast.c`: replace flat-color floor/ceiling with textured floor/ceiling sampling; choose sector texture by ray hit sector.
- `Assets/Levels/*.json`: update all `tex`, `floor_tex`, `ceil_tex` to reference filenames present in `Assets/Images/Textures/`.

Validation strategy:

- Build + run + load example maps; verify no missing textures and floor/ceiling are textured.
- Add/extend a focused test for texture path resolution and image decoding if the codebase has a natural place for it.
