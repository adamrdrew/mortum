\# Implementation Plan: Remove Entity System

\*\*Branch\*\*: `004-remove-entity-system` | \*\*Date\*\*: 2025-12-22 | \*\*Spec\*\*: `specs/004-remove-entity-system/spec.md`
\*\*Input\*\*: Feature specification from `specs/004-remove-entity-system/spec.md`

## Summary

Remove the runtime entity system from the codebase completely: delete its module(s), delete or refactor any dependent modules, and ensure there are no remaining occurrences of `entity` (case-insensitive) in any `.c`/`.h`. The game must still build and run, load a level, render, accept input, and remain stable for a smoke test.

This plan intentionally treats the “string-ban” requirement as a hard gate verified at the end.

## Technical Context

**Language/Version**: C11 (Makefile uses `-std=c11`)  
**Primary Dependencies**: SDL2 (window/input/audio), FluidSynth (MIDI playback), vendored LodePNG  
**Storage**: JSON assets on disk (`Assets/Levels/*.json`, `Assets/Timelines/*.json`, images/sounds)  
**Testing**: No automated tests; rely on `make`, `make validate`, and a manual smoke test  
**Target Platform**: macOS (current), plus SDL2-supported platforms (Linux/Windows)  
**Project Type**: Single C executable built by `Makefile`  
**Performance Goals**: 60 FPS target fixed timestep update  
**Constraints**: Keep build simple (no new deps); preserve module boundaries  
**Scale/Scope**: Medium refactor touching asset loading + game loop + rendering + gameplay modules

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Source of truth: `.specify/memory/constitution.md`

- Readability: simplest correct approach; avoid clever tricks.
- Safety: no undefined behavior knowingly; warnings treated seriously.
- Ownership: every allocation/resource has an owner; init/destroy is explicit.
- Boundaries: gameplay ↔ platform separation; isolate `#ifdef` usage.
- Builds/docs: keep `make` workflow boring; avoid painful dependencies.

\*\*Gate status\*\*: PASS (no planned violations).

## Project Structure

### Documentation (this feature)

```text
specs/004-remove-entity-system/
├── plan.md
├── research.md
├── data-model.md
├── quickstart.md
├── contracts/
└── tasks.md             # created by /speckit.tasks (not by /speckit.plan)
```

### Source Code (repository root)
```text
Assets/
docs/
include/
  assets/
  core/
  game/
  platform/
  render/
src/
  assets/
  core/
  game/
  platform/
  render/
third_party/
tools/
```

**Structure Decision**: Single C project with headers in `include/` and implementation in `src/`, built via `Makefile`.

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| [e.g., 4th project] | [current need] | [why 3 projects insufficient] |
| [e.g., Repository pattern] | [specific problem] | [why direct DB access insufficient] |

\*\*Complexity notes\*\*: None.

## Phase 0: Outline & Research (completed)

Artifacts:
- `specs/004-remove-entity-system/research.md`

Key outcomes:
- Decide how to preserve “playable” while removing the system.
- Define verification gates (string-ban + build/run smoke).
- Define doc-sweep scope and assumptions.

## Phase 1: Design & Contracts (completed)

Artifacts:
- `specs/004-remove-entity-system/data-model.md`
- `specs/004-remove-entity-system/contracts/map-json.md`
- `specs/004-remove-entity-system/quickstart.md`

Design summary:
- Remove the `Entity`/`EntityList` module.
- Update map loading results to exclude dynamic-object state.
- Delete or refactor any dependent gameplay and rendering modules.

## Phase 1: Update Agent Context (required)

Run:
- `.specify/scripts/bash/update-agent-context.sh copilot`

## Phase 2: Implementation Planning (what to build)

### Step 1 — Inventory + gates

- Enumerate every reference to the removed system across `.c`/`.h` (includes, types, function names, comments/strings).
- Decide which dependent modules are deleted vs refactored based on whether they can function without the removed abstraction.

### Step 2 — Delete the system module

- Delete `include/game/entity.h`.
- Delete `src/game/entity.c`.
- Remove those files from `Makefile`.

Acceptance gate:
- The deleted headers are not included anywhere.

### Step 3 — Remove it from asset loading types

- Update `include/assets/map_loader.h`:
  - Remove the dynamic-object list field from `MapLoadResult`.
  - Update `map_load_result_destroy` expectations accordingly.
- Update `src/assets/map_loader.c` to match.

Acceptance gate:
- `map_load` still loads world geometry and player start successfully.

### Step 4 — Remove it from the main loop and gameplay pipeline

- Update `src/main.c` to remove:
  - billboard rendering dependency (`src/render/entities.c` and its header), and
  - any gameplay updates that require the removed system.

This will typically require either deletion or refactor of:
- `src/render/entities.c` (+ its header in `include/render/` if present)
- gameplay modules that take the removed list type as a parameter

Acceptance gate:
- The executable still launches and can load a level and run the frame loop.

### Step 5 — Refactor or delete dependent gameplay modules

Two implementation options:

\*\*Option A (minimal)\*\*: delete dependent gameplay features (spawns, foes, pickups, gates, exit triggers, hazards) and keep world + player + HUD + weapon/viewmodel stable.

\*\*Option B (bigger refactor)\*\*: replace the shared list with dedicated structures per subsystem (foes/shots/loot/locks/hazards/finish marker), ensuring none of the removed-system naming remains.

Default choice for this branch: Option A unless gameplay regressions require Option B.

### Step 6 — Documentation sweep

- Update `README.md` and `docs/ARCHITECTURE.md` so they do not mention the removed system.

### Step 7 — Final verification

Hard gates:
- Search for case-insensitive `entity` across all `.c` and `.h` under `src/` and `include/` returns zero results.
- `make clean && make` succeeds.
- `make run` succeeds and passes the smoke test in `quickstart.md`.
