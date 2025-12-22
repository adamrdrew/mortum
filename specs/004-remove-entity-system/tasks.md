---

description: "Task list for removing the entity system"

---

# Tasks: Remove Entity System

**Input**: Design documents in `specs/004-remove-entity-system/`

**Notes / Constraints**
- The end state must contain **zero** case-insensitive matches for `entity` in any `.c` or `.h` under `src/` and `include/`.
- Do not leave “removed X” comments behind; remove code and headers outright.
- If a `.c`/`.h` is purely part of the removed system, delete it (no empty shells).
- Documentation scope for the shipped project docs is `README.md` + `docs/*.md`.

## Phase 1: Setup (Shared Infrastructure)

- [x] T001 Baseline build/run smoke per Makefile and specs/004-remove-entity-system/quickstart.md
- [x] T002 Capture current banned-string inventory using specs/004-remove-entity-system/quickstart.md (search `src/**/*.c`, `src/**/*.h`, `include/**/*.h`)

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Make the project compile without the removed system by deleting modules and removing all call-sites.

- [x] T003 Update MapLoadResult to remove the dynamic-object list in include/assets/map_loader.h
- [x] T004 Update map loader to stop initializing/destroying the removed list and stop requiring an `entities` JSON field in src/assets/map_loader.c

- [x] T005 [P] Delete the removed system public header include/game/entity.h
- [x] T006 [P] Delete the removed system implementation src/game/entity.c
- [x] T007 Update build inputs to remove src/game/entity.c from Makefile

- [x] T008 [P] Delete billboard header include/render/entities.h
- [x] T009 [P] Delete billboard implementation src/render/entities.c
- [x] T010 Update build inputs to remove src/render/entities.c from Makefile

- [x] T011 Update the main loop to remove billboard includes/usage in src/main.c

- [x] T012 [P] Delete enemy subsystem files include/game/enemy.h and src/game/enemy.c
- [x] T013 [P] Delete projectile subsystem files include/game/projectiles.h and src/game/projectiles.c
- [x] T014 [P] Delete pickup subsystem files include/game/pickups.h and src/game/pickups.c
- [x] T015 [P] Delete gates subsystem files include/game/gates.h and src/game/gates.c
- [x] T016 [P] Delete exit subsystem files include/game/exit.h and src/game/exit.c
- [x] T017 [P] Delete corruption subsystem files include/game/corruption.h and src/game/corruption.c
- [x] T018 [P] Delete undead-mode subsystem files include/game/undead_mode.h and src/game/undead_mode.c
- [x] T019 [P] Delete drops subsystem files include/game/drops.h and src/game/drops.c
- [x] T020 [P] Delete debug spawn subsystem files include/game/debug_spawn.h and src/game/debug_spawn.c
- [x] T021 [P] Delete combat helper subsystem files include/game/combat.h and src/game/combat.c

- [x] T022 Update build inputs to remove deleted gameplay modules from Makefile (src/game/enemy.c, src/game/projectiles.c, src/game/pickups.c, src/game/gates.c, src/game/exit.c, src/game/corruption.c, src/game/undead_mode.c, src/game/drops.c, src/game/debug_spawn.c, src/game/combat.c)
- [x] T023 Update the main loop to remove includes and calls to deleted gameplay modules in src/main.c

- [x] T024 Refactor the weapon system API to not take/return removed-system types in include/game/weapons.h
- [x] T025 Refactor weapon logic to remove spawning of dynamic objects while keeping weapon selection, cooldown, ammo consumption, and fire input handling stable in src/game/weapons.c
- [x] T026 Update call-sites for the new weapon API in src/main.c

- [x] T027 Make the project build again: resolve any remaining compile errors introduced by deletions/renames in src/main.c and include/game/*.h

**Checkpoint**: `make clean && make` succeeds.

---

## Phase 3: User Story 1 - Keep the game playable after refactor (Priority: P1) 🎯 MVP

**Goal**: Launch, load a level, move/look, render the world, and interact with core controls without crashes.

**Independent Test**: Run the smoke test in specs/004-remove-entity-system/quickstart.md.

- [x] T028 [US1] Ensure HUD remains functional without removed subsystems (e.g., keys/locks/loot indicators) by updating src/game/hud.c and include/game/hud.h
- [x] T029 [US1] Ensure weapon viewmodel rendering remains functional after weapon API changes by updating src/game/weapon_view.c and include/game/weapon_view.h
- [x] T030 [US1] Validate player controller + movement loop works without removed modules; fix any fallout in src/game/player_controller.c and src/game/player.c
- [x] T031 [US1] Verify level geometry rendering path remains intact and fix any compile/runtime fallout in src/render/raycast.c and src/render/level_mesh.c

**Checkpoint**: `make run` loads a level and remains stable during the 10-minute smoke test.

---

## Phase 4: User Story 2 - Remove entity-system surface area (Priority: P2)

**Goal**: Remove every remaining trace from `.c`/`.h`, delete any remaining orphaned files, and ensure headers expose no removed-system API.

**Independent Test**: Case-insensitive search for `entity` across `.c`/`.h` under `src/` and `include/` returns zero matches; clean build succeeds.

- [x] T032 [US2] Remove banned-string occurrences from comments/strings in include/game/weapon_visuals.h and src/game/weapon_visuals.c
- [x] T033 [US2] Remove banned-string occurrences from comments/strings in include/render/lighting.h and src/render/lighting.c (if present)
- [x] T034 [US2] Re-run the banned-string scan and remove any remaining matches in src/**/*.c, src/**/*.h, and include/**/*.h
- [x] T035 [US2] Delete any leftover files that became empty or were only supporting the removed system (verify under src/game/ and include/game/)

**Checkpoint**: The banned-string scan reports zero matches and `make clean && make` succeeds.

---

## Phase 5: User Story 3 - Preserve non-entity features (Priority: P3)

**Goal**: Level loading, rendering, input, audio, UI/HUD, and core weapon handling continue to work.

**Independent Test**: Manual regression pass: load 2 different maps in one session (or restart), verify input response, rendering, HUD, and audio stability.

- [x] T036 [US3] Update the asset validator tool for the MapLoadResult change in tools/validate_assets.c
- [x] T037 [US3] Verify map loading still accepts current content format (including ignoring unknown keys) by checking src/assets/map_loader.c against specs/004-remove-entity-system/contracts/map-json.md
- [x] T038 [US3] Run `make run MAP=arena.json` and `make run MAP=e1m1.json` (or two known-good maps) per specs/004-remove-entity-system/quickstart.md

---

## Phase 6: Polish & Cross-Cutting Concerns

- [x] T039 [P] Remove mentions from shipped documentation in README.md
- [x] T040 [P] Remove mentions from shipped documentation in docs/ARCHITECTURE.md
- [x] T041 [P] Remove mentions from shipped documentation in docs/CONTRIBUTING.md
- [x] T042 Run final full verification: banned-string scan + `make clean && make` + `make run` per specs/004-remove-entity-system/quickstart.md

---

## Dependencies & Execution Order

- Setup (Phase 1) → Foundational (Phase 2) → US1 (Phase 3) → US2 (Phase 4) → US3 (Phase 5) → Polish (Phase 6)

### User Story Completion Order

- US1 depends on Phase 2 (compile without removed system)
- US2 depends on US1 being stable enough to validate removals without chasing crashes
- US3 depends on US2 (so regression tests aren’t invalidated by further removals)

## Parallel Execution Examples

### Foundational

You can do these in parallel:
- Task: "Delete billboard header include/render/entities.h"
- Task: "Delete billboard implementation src/render/entities.c"
- Task: "Delete the removed system public header include/game/entity.h"
- Task: "Delete the removed system implementation src/game/entity.c"

### US2 cleanup

You can do these in parallel:
- Task: "Remove banned-string occurrences from comments/strings in include/game/weapon_visuals.h and src/game/weapon_visuals.c"
- Task: "Remove banned-string occurrences from comments/strings in include/render/lighting.h and src/render/lighting.c (if present)"

## Implementation Strategy

### MVP scope (recommended)

- Complete Phase 1 + Phase 2 (build succeeds)
- Complete US1 (Phase 3) and stop
- Validate the smoke test before continuing

### Incremental delivery

- After US1 is stable, do US2 to enforce the hard “zero matches” gate
- Finish with US3 regression and Phase 6 doc sweep + final verification
