---

description: "Tasks for Weapon Viewmodels, Icons, and Switching"
---

# Tasks: Weapon Viewmodels, Icons, and Switching

**Input**: Design documents from `/specs/003-weapon-system/`

**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/

**Tests**: Not requested for this feature; rely on `make test` (existing) + the independent visual tests below.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Establish the minimal modules/APIs needed for weapon visuals.

- [X] T001 Create weapon visual mapping header in include/game/weapon_visuals.h
- [X] T002 Implement weapon visual mapping helpers in src/game/weapon_visuals.c

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core infrastructure that MUST be complete before ANY user story work can be verified.

- [X] T003 [P] Add alpha-blended blit API for sprites in include/render/draw.h
- [X] T004 Implement alpha-blended blit for ABGR8888 pixels in src/render/draw.c
- [X] T005 [P] Relax PNG 64x64 enforcement to allow weapon sprites/icons in src/render/texture.c
- [X] T006 [P] Extend weapon IDs to 5 supported weapons in include/game/weapon_defs.h
- [X] T007 Update weapon definitions for Handgun/Shotgun/Rifle/SMG/Rocket in src/game/weapon_defs.c
- [X] T008 Support new weapon pickup entity strings (and keep old aliases) in src/game/pickups.c
- [X] T009 [P] Add player velocity + weapon viewmodel state fields to Player in include/game/player.h
- [X] T010 Initialize new Player fields in src/game/player.c

**Checkpoint**: Foundation ready — user story implementation can now begin.

---

## Phase 3: User Story 1 — See current weapon in HUD and hands (Priority: P1) 🎯 MVP

**Goal**: Show equipped weapon via HUD icon + first-person idle viewmodel; render weapon pickups using the new PNG assets.

**Independent Test**: Run `make run RUN_MAP=arena.json` and verify:
- Picking up Shotgun/Rifle/SMG updates the equipped weapon.
- The viewmodel switches immediately to the equipped weapon’s IDLE sprite.
- The weapon viewmodel is drawn before the HUD and its bottom edge is slightly obscured by the HUD bar.
- The HUD displays the equipped weapon icon.
- Weapon pickups in the world render with the weapon PNG pickup sprites (not the `sprites.bmp` atlas stand-in).

### Implementation for User Story 1

- [X] T011 [P] [US1] Add weapon viewmodel draw API header in include/game/weapon_view.h
- [X] T012 [US1] Implement weapon viewmodel draw (IDLE frame) in src/game/weapon_view.c
- [X] T013 [US1] Extend HUD draw signature to accept textures/paths in include/game/hud.h
- [X] T014 [US1] Draw equipped weapon icon in the HUD bar in src/game/hud.c
- [X] T015 [US1] Draw the weapon viewmodel before HUD (behind HUD overlap) in src/main.c
- [X] T016 [US1] Update the HUD call site for new signature in src/main.c
- [X] T017 [P] [US1] Render weapon pickup billboards using `WEAPON-PICKUP.png` in src/render/entities.c

**Checkpoint**: US1 is fully functional and testable independently.

---

## Phase 4: User Story 2 — Weapon sways while moving (Priority: P2)

**Goal**: Add movement-driven sway/bob so the weapon viewmodel feels alive.

**Independent Test**: In `arena.json`, walk forward/strafe continuously and verify:
- The weapon viewmodel smoothly sways while moving.
- When stopping, sway settles back toward idle.

### Implementation for User Story 2

- [X] T018 [US2] Persist player velocity each frame for sway inputs in src/game/player_controller.c
- [X] T019 [US2] Apply sway/bob offsets when drawing viewmodel in src/game/weapon_view.c

**Checkpoint**: US2 is fully functional and testable independently.

---

## Phase 5: User Story 3 — Switching and firing plays correct animation (Priority: P3)

**Goal**: Add 1–5 weapon selection, Q/E weapon cycling, and 6-frame shoot animation on successful shots.

**Independent Test**: In `arena.json`, verify:
- Number keys 1–5 select specific weapons (only if owned).
- Q cycles to previous owned weapon; E cycles to next owned weapon.
- On successful firing (ammo consumed and projectile spawned), the viewmodel plays SHOOT-1..6 and returns to IDLE.
- If a weapon has no ammo, firing does not start the shoot animation.

### Implementation for User Story 3

- [X] T020 [US3] Update weapons API documentation for 1–5 selection and Q/E cycling in include/game/weapons.h
- [X] T021 [US3] Update weapon switching logic to support 5 weapons + Q/E cycle inputs in src/game/weapons.c
- [X] T022 [US3] Add key bindings for 1–5 select and Q/E cycle in src/main.c
- [X] T023 [US3] Rebind purge-item use key from E to F in src/main.c
- [X] T024 [US3] Record successful-fire events to trigger animation in src/game/weapons.c
- [X] T025 [US3] Implement SHOOT-1..6 animation stepping + render selection in src/game/weapon_view.c

**Checkpoint**: US3 is fully functional and testable independently.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Small improvements and validation after the main user stories.

- [X] T026 [P] Add/adjust weapon pickup entities (Rocket/Handgun if desired) in Assets/Levels/arena.json
- [X] T027 Validate quickstart steps and update docs if needed in specs/003-weapon-system/quickstart.md

---

## Dependencies & Execution Order

### User Story Completion Order

1. Phase 1 → Phase 2 (blocking)
2. US1 (P1) depends on Foundational
3. US2 (P2) depends on US1 (needs viewmodel in place)
4. US3 (P3) depends on US1 (adds switching + firing animation on top)

### Parallel Opportunities (within this feature)

- Within Foundational:
  - T003 and T005 can proceed in parallel (different files).
  - T006 and T009 can proceed in parallel (different headers).
- Within US1:
  - T012 (viewmodel implementation) and T014 (HUD icon) can proceed in parallel.
  - T017 (world pickup rendering) can proceed in parallel with most US1 work.

---

## Parallel Example: User Story 1

Run these in parallel (different files, minimal dependency):

- Task: "T012 Implement weapon viewmodel draw (IDLE frame) in src/game/weapon_view.c"
- Task: "T014 Draw equipped weapon icon in the HUD bar in src/game/hud.c"
- Task: "T017 Render weapon pickup billboards using `WEAPON-PICKUP.png` in src/render/entities.c"

---

## Implementation Strategy

### MVP First (User Story 1 Only)

1. Complete Setup + Foundational
2. Complete US1
3. STOP and validate US1 independently in `arena.json`

### Incremental Delivery

- Add US2 (sway) after US1 feels stable.
- Add US3 (switching + shoot animation) last, since it touches input and timing.
