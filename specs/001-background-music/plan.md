# Implementation Plan: [FEATURE]

**Branch**: `[###-feature-name]` | **Date**: [DATE] | **Spec**: [link]
**Input**: Feature specification from `/specs/[###-feature-name]/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

[Extract from feature spec: primary requirement + technical approach from research]

## Technical Context

<!--
  ACTION REQUIRED: Replace the content in this section with the technical details
  for the project. The structure here is presented in advisory capacity to guide
  the iteration process.
-->

**Language/Version**: C99 (cross-platform)
**Primary Dependencies**: Fluidsynth (libfluidsynth, fluidsynth.h)
**Storage**: JSON map files (Assets/Levels/*.json), MIDI files, SoundFonts
**Testing**: Manual playtest, automated level load/playback tests (NEEDS CLARIFICATION: test framework)
**Target Platform**: macOS, Linux, Windows
**Project Type**: Single binary, old-school shooter
**Performance Goals**: Music playback must start/stop within 100ms of level change or quit; no audio glitches
**Constraints**: No SDL for music; only Fluidsynth; minimal dependencies; must not block game loop
**Scale/Scope**: All playable levels; 30+ MIDI files; 1 default soundfont; extensible for future maps

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

Source of truth: `.specify/memory/constitution.md`

Readability: Use clear, simple C for music integration; avoid clever macros or opaque logic.
Safety: All Fluidsynth resources (synth, player, settings) must be explicitly owned and released; error handling for missing/corrupt files.
Ownership: Each music playback instance is created/destroyed per level; no leaks; explicit shutdown on quit.
Boundaries: Music system is isolated from gameplay logic; platform-specific code (if any) is minimal and contained.
Builds/docs: Makefile updated for Fluidsynth; install steps documented in quickstart; no unnecessary dependencies.

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
src/
  assets/
    midi_player.c        # Fluidsynth integration, MIDI playback
    midi_player.h        # API for music control
    asset_paths.c/h      # Path helpers for MIDI/SoundFont
  game/
    level.c/h            # Level load/unload, bgmusic triggers
    game_loop.c/h        # Music start/stop on level change/quit
include/
  assets/
    midi_player.h        # Header for music API
  game/
    level.h              # Level struct with bgmusic/soundfont fields
    game_loop.h          # Game loop hooks for music
Makefile                 # Add Fluidsynth build/link
Assets/Sounds/MIDI/      # MIDI files
Assets/Sounds/SoundFonts/# SoundFonts
  not include Option labels.
-->

```text
# [REMOVE IF UNUSED] Option 1: Single project (DEFAULT)
src/
├── models/
├── services/
├── cli/
└── lib/

tests/
├── contract/
├── integration/
└── unit/

# [REMOVE IF UNUSED] Option 2: Web application (when "frontend" + "backend" detected)
backend/
├── src/
│   ├── models/
│   ├── services/
│   └── api/
└── tests/

frontend/
├── src/
│   ├── components/
│   ├── pages/
│   └── services/
└── tests/

# [REMOVE IF UNUSED] Option 3: Mobile + API (when "iOS/Android" detected)
api/
└── [same as backend above]

ios/ or android/
└── [platform-specific structure: feature modules, UI flows, platform tests]
```

**Structure Decision**: [Document the selected structure and reference the real
directories captured above]

## Complexity Tracking

> **Fill ONLY if Constitution Check has violations that must be justified**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| [e.g., 4th project] | [current need] | [why 3 projects insufficient] |
| [e.g., Repository pattern] | [specific problem] | [why direct DB access insufficient] |
