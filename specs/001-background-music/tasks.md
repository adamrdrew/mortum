# Tasks: Background Music

**Input**: Design documents from `/specs/001-background-music/`
**Prerequisites**: plan.md (required), spec.md (required for user stories), research.md, data-model.md, contracts/

## Phase 1: Setup (Shared Infrastructure)

- [ ] T001 Create midi_player.c and midi_player.h in src/assets/ for Fluidsynth integration
- [ ] T002 [P] Add Fluidsynth dependency to Makefile and document install in quickstart.md
- [ ] T003 [P] Create asset_paths.c/h for MIDI/SoundFont path helpers in src/assets/
- [ ] T004 [P] Update include/assets/midi_player.h for music API
- [ ] T005 [P] Create initial test MIDI and SoundFont files in Assets/Sounds/MIDI/ and Assets/Sounds/SoundFonts/

---

## Phase 2: Foundational (Blocking Prerequisites)

- [ ] T006 Implement error handling for missing/corrupt MIDI/SoundFont in src/assets/midi_player.c
- [ ] T007 [P] Add bgmusic and soundfont fields to Level struct in include/game/level.h
- [ ] T008 [P] Update level loader in src/game/level.c to parse bgmusic/soundfont from JSON
- [ ] T009 [P] Add validation for MIDI/SoundFont existence in src/game/level.c
- [ ] T010 [P] Ensure only one music track plays at a time in src/assets/midi_player.c

---

## Phase 3: User Story 1 - Level-Specific Music (Priority: P1) 🎯 MVP

**Goal**: Play background music unique to each level on loop
**Independent Test**: Entering any level starts its specific music, which loops until the level changes or game quits

- [ ] T011 [P] [US1] Implement music start/stop API in src/assets/midi_player.c
- [ ] T012 [P] [US1] Integrate music start on level load in src/game/level.c
- [ ] T013 [US1] Add loop logic for MIDI playback in src/assets/midi_player.c
- [ ] T014 [US1] Manual test: Enter level and verify music plays/loops

---

## Phase 4: User Story 2 - Seamless Music Transition (Priority: P2)

**Goal**: Stop old music and start new music immediately on level change
**Independent Test**: Changing levels stops old music and starts new music without delay or overlap

- [ ] T015 [P] [US2] Implement music stop/start on level change in src/game/game_loop.c
- [ ] T016 [US2] Ensure no overlap or delay in src/assets/midi_player.c
- [ ] T017 [US2] Manual test: Rapid level changes, verify only current level's music plays

---

## Phase 5: User Story 3 - Music Stops on Quit (Priority: P3)

**Goal**: Stop all music playback when quitting the game
**Independent Test**: Quitting the game stops all music playback

- [ ] T018 [P] [US3] Integrate music stop on quit in src/game/game_loop.c
- [ ] T019 [US3] Manual test: Quit game, verify music stops

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T020 [P] Documentation updates in specs/001-background-music/quickstart.md and docs/
- [ ] T021 Code cleanup and refactoring in src/assets/midi_player.c/h
- [ ] T022 Performance optimization for music playback in src/assets/midi_player.c
- [ ] T023 [P] Additional manual tests for edge cases (missing MIDI, missing SoundFont, rapid level change)
- [ ] T024 Run quickstart.md validation

---

## Dependencies & Execution Order

- Setup (Phase 1): No dependencies
- Foundational (Phase 2): Depends on Setup completion
- User Stories (Phase 3+): All depend on Foundational phase completion
- Polish (Final Phase): Depends on all user stories being complete

### User Story Completion Order
- US1 → US2 → US3

### Parallel Execution Examples
- T002, T003, T004, T005 can run in parallel
- T007, T008, T009, T010 can run in parallel
- T011, T012 can run in parallel
- T015, T018 can run in parallel
- T020, T023 can run in parallel

## Implementation Strategy
- MVP: Complete Phase 1, Phase 2, and US1 (Phase 3)
- Incremental: Add US2, US3, then Polish
- Each user story is independently testable

## Task Format Validation
- All tasks use checklist format: `- [ ] TXXX [P] [USX] Description with file path`
- Each user story phase is independently testable
- Parallel tasks marked [P] and use different files
- File paths are explicit
