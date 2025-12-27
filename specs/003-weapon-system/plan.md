# Implementation Plan: Weapon Viewmodels, Icons, and Switching

**Branch**: `003-weapon-system` | **Date**: 2025-12-20 | **Spec**: `specs/003-weapon-system/spec.md`
**Input**: Feature specification from `/specs/003-weapon-system/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

Add a first-person weapon “viewmodel” layer (idle + 6-frame shoot animation), HUD weapon icons, and weapon pickup rendering using the new PNG weapon assets under `Assets/Images/Weapons/`. Update weapon switching to support 1–5 selection plus Q/E cycling, and add a simple movement-driven sway.

## Technical Context

<!--
  ACTION REQUIRED: Replace the content in this section with the technical details
  for the project. The structure here is presented in advisory capacity to guide
  the iteration process.
-->

**Language/Version**: C11 (clang primary, gcc secondary)  
**Primary Dependencies**: SDL2 (window/input/audio)  
**Storage**: JSON files in `Assets/` (maps, episodes); PNG assets in `Assets/Images/`  
**Testing**: `make test` (C tests) + in-game visual sanity checks  
**Target Platform**: macOS, Linux, Windows (MinGW)  
**Project Type**: single C game executable (software framebuffer renderer)  
**Performance Goals**: 60 FPS at default internal resolution; avoid per-frame heap churn in hot paths  
**Constraints**: minimal deps; keep code readable C; preserve gameplay↔platform boundaries; log failures without per-frame spam  
**Scale/Scope**: weapon visuals + switching; no new editor tooling; no new rendering backend

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Source of truth: `.specify/memory/constitution.md`

- Readability: simplest correct approach; avoid clever tricks.
- Safety: no undefined behavior knowingly; warnings treated seriously.
- Ownership: every allocation/resource has an owner; init/destroy is explicit.
- Boundaries: gameplay ↔ platform separation; isolate `#ifdef` usage.
- Builds/docs: keep `make` workflow boring; avoid painful dependencies.

GATE STATUS: PASS (small, local changes; no new heavy deps; rendering changes stay inside `render/` and UI stays in `game/`).

## Project Structure

### Documentation (this feature)

```text
specs/003-weapon-system/
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
  game/
    weapons.c
    weapon_defs.c
    pickups.c
    hud.c
    player_controller.c
    player.c
  render/
    entities.c
    texture.c
    draw.c

include/
  game/
    weapons.h
    weapon_defs.h
    pickups.h
    player.h
    hud.h
  render/
    entities.h
    texture.h
    draw.h
  assets/
    asset_paths.h

Assets/
  Images/
    Weapons/
      Handgun/
      Shotgun/
      Rifle/
      SMG/
      Rocket/
  Levels/
```

**Structure Decision**: Single C project. Weapon visuals are split cleanly: gameplay lives in `src/game/*` (switching, cooldowns, animation state), and rendering utilities live in `src/render/*` (texture caching, alpha blit, entity billboards).

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| *(none)* |  |  |

## Phase 0 — Research (resolve open questions)

1. Confirm existing weapon/gameplay baseline and integration points:
  - `weapons_update` already handles switching and firing logic.
  - Pickups already grant weapons via `pickup_weapon_*` entities.
2. Determine how to load weapon PNG assets without new systems:
  - Use `TextureRegistry` with relative paths under `Assets/Images/`.
  - Relax PNG 64x64 enforcement so weapon sprites are allowed.
3. Confirm draw capabilities for PNG sprites:
  - Add alpha-blended blit helper (weapon sprites/icons require transparency).
4. Resolve input conflicts:
  - Implement Q/E cycling; move purge-item action off E.

Output: `specs/003-weapon-system/research.md`.

## Phase 1 — Design (data model + contracts)

1. Data model changes:
  - Extend `WeaponId` to 5 weapons (Handgun, Shotgun, Rifle, SMG, Rocket).
  - Add `WeaponVisual` mapping from `WeaponId` to icon/pickup/viewmodel frame paths.
  - Add `WeaponViewState` for sway + shooting animation frame stepping.
  - Store player velocity (`vx,vy`) to drive sway.
2. Rendering design:
  - Weapon viewmodel is drawn in screen space before HUD; HUD obscures weapon bottom slightly.
  - HUD draws equipped weapon icon.
  - World pickups for weapons render using `WEAPON-PICKUP.png` billboards.
3. Contracts:
  - Add a JSON schema for an optional weapon-asset manifest (keeps naming conventions explicit).

Outputs:
- `specs/003-weapon-system/data-model.md`
- `specs/003-weapon-system/contracts/weapon_assets.schema.json`
- `specs/003-weapon-system/quickstart.md`

## Phase 1 — Update Agent Context

Run `.specify/scripts/bash/update-agent-context.sh copilot` after design artifacts exist.

## Phase 2 — Implementation Plan (high-level steps)

1. Weapons & input
  - Extend `WeaponId`/`WEAPON_COUNT` to include Rocket and rename/migrate existing IDs as needed.
  - Update `src/main.c` input mapping: 1–5 select specific weapons; Q/E cycle owned weapons.
  - Move purge-item action from E to F (or another non-conflicting key).
2. Weapon visuals
  - Define weapon visual asset paths and load via `TextureRegistry` using `Assets/Images/Weapons/...`.
  - Add `WeaponViewState` update: sway driven by player movement; shoot animation triggered on successful shot.
  - Add alpha-blended blit helper for PNG sprites.
  - Draw viewmodel before HUD with a small HUD overlap; draw icon inside HUD.
3. World pickup rendering
  - Render weapon pickup entities using `WEAPON-PICKUP.png` rather than the sprites atlas.
4. Texture system adjustment
  - Relax the PNG 64x64 enforcement so weapon sprites/icons are not rejected.
  - Preserve caching behavior (including cached misses) to avoid per-frame disk I/O.

STOP CONDITION: This plan phase ends here; implementation is executed in follow-up work.

## Constitution Check (Post-Design Re-check)

- Still PASS: changes remain modular, avoid new dependencies, and keep rendering + gameplay boundaries intact.
