---

description: "Task list for Texture Floors/Ceilings + New Texture Library"

---

# Tasks: Texture Floors/Ceilings + New Texture Library

**Input**: Design documents from `specs/002-texture-floors-ceilings/`
**Prerequisites**: `plan.md` (required), `spec.md` (required), `research.md`, `data-model.md`, `contracts/`

**Tests**: Not generating dedicated automated test tasks (current `tests/` is empty). Validation is via build + running updated example maps.

## Phase 1: Setup (Shared Infrastructure)

- [x] T001 Capture baseline build/run output using `Makefile` (run `make clean && make && make run`)
- [x] T002 Inventory available textures in `Assets/Images/Textures/` and pick a small set to use in example maps (record choices in `specs/002-texture-floors-ceilings/research.md`)

---

## Phase 2: Foundational (Blocking Prerequisites)

- [x] T003 Add PNG loading API in `include/assets/image.h` (declare `image_load_png()` and/or `image_load_auto()`)
- [x] T004 Add PNG decoding via vendored LodePNG (no SDL2_image) and wire it into the build in `Makefile`
- [x] T005 Implement `image_load_png()` in `src/assets/image_png.c` (decode via LodePNG, convert to ABGR8888 to match framebuffer)
- [x] T006 Align image pixel-format contract in `include/assets/image.h` and `src/assets/image_bmp.c` (ensure comments + conversions match ABGR8888)
- [x] T007 Update texture load path to prefer `Assets/Images/Textures/` in `src/render/texture.c` (optional fallback to `Assets/Images/`)
- [x] T008 Add extension-based decoding in `src/render/texture.c` (use PNG loader for `.PNG`/`.png`, BMP loader for `.bmp`)
- [x] T009 Enforce 64x64 textures in `src/render/texture.c` (log error + return NULL/fallback when unexpected size)
- [x] T010 Strengthen map validation for texture fields in `src/assets/map_validate.c` (non-empty `Sector.floor_tex`/`Sector.ceil_tex` and `Wall.tex`)

**Checkpoint**: A wall texture referenced as `BRICK_1A.PNG` loads from `Assets/Images/Textures/` and renders (still with flat floor/ceiling).

---

## Phase 3: User Story 1 — Textured floors & ceilings (Priority: P1) 🎯 MVP

**Goal**: Render textured floors and ceilings per sector using `floor_tex` / `ceil_tex`.

**Independent Test**: Run `make run` (see `Makefile`) and load `Assets/Levels/arena.json`; floor and ceiling should be textured (not flat fills).

- [x] T011 [US1] Remove flat-color floor/ceiling background fills in `src/render/raycast.c` (replace with textured floor/ceiling drawing)
- [x] T012 [US1] Implement floor-casting math per column in `src/render/raycast.c` (compute world position per screen pixel for floor rows)
- [x] T013 [US1] Sample sector floor textures via `TextureRegistry` in `src/render/raycast.c` (use `Sector.floor_tex`)
- [x] T014 [US1] Implement ceiling texture sampling in `src/render/raycast.c` (use `Sector.ceil_tex`)
- [x] T015 [US1] Apply lighting to floor/ceiling samples in `src/render/raycast.c` (reuse `lighting_apply()` with sector light + point lights)
- [x] T016 [US1] Ensure `out_depth` behavior remains valid for sprites in `src/render/raycast.c` (floor/ceiling drawing must not break depth usage)
- [ ] T017 [US1] Manual verify textured floor/ceiling in `Assets/Levels/arena.json` (run `make run` from `Makefile`)

---

## Phase 4: User Story 2 — Maps can reference the new texture library (Priority: P2)

**Goal**: Any map can reference filenames from `Assets/Images/Textures/` for walls, floors, ceilings.

**Independent Test**: Change a wall texture in `Assets/Levels/mortum_test.json` to a known PNG filename and confirm it renders and missing textures log clearly.

- [x] T018 [US2] Improve missing-texture diagnostics in `src/render/texture.c` (log attempted paths and filename)
- [x] T019 [US2] Update texture loader documentation comment in `include/render/texture.h` to reference `Assets/Images/Textures/` and PNG support
- [x] T020 [US2] Verify `.PNG` filename case handling works on case-sensitive filesystems (audit call sites in `src/render/texture.c` and map JSON under `Assets/Levels/`)

---

## Phase 5: User Story 3 — Example maps showcase textures (Priority: P3)

**Goal**: Update all example maps to use the new texture set.

**Independent Test**: Load each map in `Assets/Levels/` and confirm no missing-texture errors and visible texture variety.

- [x] T021 [P] [US3] Update `Assets/Levels/arena.json` to use `.PNG` textures from `Assets/Images/Textures/` for walls + sector `floor_tex`/`ceil_tex`
- [x] T022 [P] [US3] Update `Assets/Levels/mortum_test.json` to use `.PNG` textures from `Assets/Images/Textures/` for walls + sector `floor_tex`/`ceil_tex`
- [x] T023 [P] [US3] Update `Assets/Levels/e1m1.json` to use `.PNG` textures from `Assets/Images/Textures/` for walls + sector `floor_tex`/`ceil_tex`
- [x] T024 [US3] Validate all updated maps load/run (use `Makefile` + files under `Assets/Levels/`)

---

## Final Phase: Polish & Cross-Cutting Concerns

- [x] T025 [P] Update architecture/docs for floor/ceiling texturing + texture directory in `docs/ARCHITECTURE.md`
- [x] T026 Ensure `specs/002-texture-floors-ceilings/quickstart.md` matches the final verification steps

---

## Dependencies & Execution Order

### User Story completion order

US1 → US2 → US3

- US1 depends on Phase 2 (PNG + texture path + decoding) being complete.
- US2 depends on Phase 2 (ability to load PNG textures by filename) being complete.
- US3 depends on US2 (maps can reference the new library) being complete.

### Parallel opportunities

- T021–T023 can run in parallel (different map files).
- T002 can run in parallel with T003–T010 (asset selection doesn’t block code).
- T025 can run in parallel with map updates once the final behavior is known.

---

## Parallel Execution Examples

### US1

- Run in parallel:
  - Implement PNG decode pipeline: T003–T006
  - Texture search path + decoding selection: T007–T009

### US3

- Run in parallel:
  - T021 (arena map)
  - T022 (mortum_test map)
  - T023 (e1m1 map)

---

## Implementation Strategy

### MVP (recommended)

1. Complete Phase 2 (textures load correctly from `Assets/Images/Textures/`).
2. Complete US1 (textured floors/ceilings). Stop and validate with `Assets/Levels/arena.json`.

### Incremental delivery

- After MVP, improve diagnostics (US2), then update example maps (US3), then polish docs.
