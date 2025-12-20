# Tasks: Rendering, HUD, and Lighting Improvements

**Input**: Design documents from `specs/001-mortum-vision-spec/` (plan.md, spec.md, research.md, data-model.md, contracts/).

**Tests**: No explicit test tasks included (the `tests/` folder is empty). Validation is via manual scenarios + `make validate`.

**Organization**: Improvements are mapped to existing user stories (primarily US1) so work remains independently testable.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Add small reusable rendering utilities needed by multiple improvements.

- [ ] T001 Create lighting helper API in include/render/lighting.h
- [ ] T002 Implement lighting helper functions in src/render/lighting.c

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Data + asset format support for sector tint and point lights.

**Checkpoint**: Maps can specify sector light tint and optional point lights without breaking existing content.

- [ ] T003 Extend Sector to include light tint fields in include/game/world.h
- [ ] T004 Implement default sector light tint initialization in src/game/world.c
- [ ] T005 [P] Update map contract to allow optional sector light tint + lights[] in specs/001-mortum-vision-spec/contracts/map.schema.json
- [ ] T006 [P] Update asset validation to accept new lighting fields in tools/validate_assets.c
- [ ] T007 Add PointLight container to World/MapLoadResult in include/game/world.h
- [ ] T008 Implement PointLight memory ownership (alloc/free) in src/game/world.c
- [ ] T009 Parse optional sector light tint from map JSON in src/assets/map_loader.c
- [ ] T010 Parse optional lights[] point lights from map JSON in src/assets/map_loader.c
- [ ] T011 Update MapLoadResult to carry lights (if not stored in World) in include/assets/map_loader.h

---

## Phase 3: User Story 1 — Complete a Level Run (Priority: P1) 🎯 MVP

**Goal**: Visual readability fixes: correct depth ordering, Doom-style labeled HUD, and believable lighting.

**Independent Test**: Run `make run RUN_MAP=arena.json` and verify:
- A nearer enemy/pickup never appears behind a farther enemy/pickup.
- A Doom-like bottom HUD bar shows labeled values: HP, MORTUM, AMMO, KEYS.
- Distant walls/enemies are darker; sector tint and point lights visibly affect shading.

### Implementation for User Story 1

- [ ] T012 [P] [US1] Fix sprite-vs-sprite occlusion by sorting visible billboards far→near in src/render/entities.c
- [ ] T013 [P] [US1] Ensure billboard occlusion still respects wall depth buffer in src/render/entities.c

- [ ] T014 [US1] Replace debug-text HUD with Doom-style bottom bar layout in src/game/hud.c
- [ ] T015 [US1] Render labeled numbers (HP/MORTUM/AMMO/KEYS) in the HUD bar in src/game/hud.c
- [ ] T016 [US1] Add simple bevel/contrast rectangles for HUD “lighting” illusion in src/game/hud.c

- [ ] T017 [US1] Apply sector light intensity + tint to wall shading in src/render/raycast.c
- [ ] T018 [US1] Apply distance shading + point lights to billboard sprites in src/render/entities.c
- [ ] T019 [US1] Wire point lights into renderer call sites in src/main.c

- [ ] T020 [P] [US1] Add a tinted sector + at least one point light to Assets/Levels/arena.json

**Checkpoint**: US1 remains playable and visuals match the new rules.

---

## Phase 4: User Story 2 — Survive Mortum Corruption and Recover (Priority: P2)

**Goal**: Ensure the new HUD communicates Mortum/Undead progress clearly during crisis play.

**Independent Test**: Run `make run RUN_MAP=mortum_test.json` and verify:
- Mortum is labeled and readable on the HUD bar.
- When Undead is active, shards progress remains visible (either as text near Mortum or an additional labeled counter).

- [ ] T021 [US2] Add Undead-mode status line into the HUD bar layout in src/game/hud.c

---

## Phase N: Polish & Cross-Cutting Concerns

**Purpose**: Documentation and cleanup for the new rendering rules.

- [ ] T022 [P] Document lighting fields (sector tint + point lights) in specs/001-mortum-vision-spec/data-model.md
- [ ] T023 [P] Update visual sanity checks in specs/001-mortum-vision-spec/quickstart.md

---

## Dependencies & Execution Order

### User Story Completion Order

- Setup (Phase 1) → Foundational (Phase 2) → US1 visuals (Phase 3) → US2 HUD clarity (Phase 4) → Polish (Final)

### Parallel Opportunities

- Phase 2: T005 and T006 can run in parallel with T003–T004.
- Phase 3: T012 and T020 can run in parallel (code vs content).
- Phase 3: T017 (walls) and T018 (billboards) can run in parallel after T001–T002 and Phase 2 parsing are complete.

---

## Parallel Example: User Story 1

Run these in parallel (different files, minimal dependency):

- Task: "T012 Fix sprite-vs-sprite occlusion by sorting visible billboards far→near in src/render/entities.c"
- Task: "T014 Replace debug-text HUD with Doom-style bottom bar layout in src/game/hud.c"
- Task: "T017 Apply sector light intensity + tint to wall shading in src/render/raycast.c"
- Task: "T020 Add a tinted sector + at least one point light to Assets/Levels/arena.json"

---

## Implementation Strategy

### MVP First

1. Complete Phase 1 + Phase 2 (lighting infra + map parsing)
2. Complete Phase 3 (US1 visuals)
3. STOP and validate using the US1 Independent Test

### Incremental Delivery

- Depth sorting first (T012–T013) → immediate correctness win
- Doom HUD next (T014–T016) → immediate clarity win
- Lighting last (T017–T019) → atmosphere/readability improvements
