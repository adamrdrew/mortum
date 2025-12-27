# Phase 0 Research — Mortum Technical Decisions

This document resolves technical choices needed to proceed with Phase 1 design. Each item records a decision, rationale, and alternatives considered.

## Platform + Window/Input/Audio

### Decision: Use SDL2 as the platform layer (initially)

- Decision: Use **SDL2** for window creation, input (keyboard/mouse), audio output, timing, and filesystem paths.
- Rationale:
  - Widely available and stable across macOS/Linux/Windows.
  - Very well-documented with lots of examples; predictable build/install story.
  - Lets gameplay and rendering remain platform-agnostic (fits constitution boundary rules).
- Alternatives considered:
  - SDL3: promising, but ecosystem/tooling and community examples are still catching up; migrate later if/when it becomes the “boring default”.
  - GLFW + other libs: would increase dependency surface (audio, filesystem, controller input would need extra choices).

## Build System

### Decision: Makefile first, with Windows via MinGW

- Decision: Maintain a **Makefile** with targets: `make`, `make release`, `make run`, `make test`, `make clean`.
- Rationale:
  - Fast iteration and minimal toolchain complexity.
  - Fits constitution: “boring builds, fool-proof docs”.
- Alternatives considered:
  - CMake: keep as a fallback if the Makefile becomes painful for Windows users.

## Data Formats (Episode + Map)

### Decision: JSON for episodes and maps

- Decision: Store **episodes** and **maps** as JSON files.
- Rationale:
  - Human-editable, tool-friendly, easy to validate.
  - Supports future editor tooling naturally.
- Alternatives considered:
  - INI/YAML: workable, but YAML ambiguity + parser complexity is not worth it early.

### Decision: Vendor a tiny JSON parser

- Decision: Vendor a small JSON parser for C (planned) rather than adding a heavy dependency.
- Rationale:
  - Keeps the build boring and self-contained.
  - JSON parsing requirements are modest (episodes + maps).
- Alternatives considered:
  - cJSON: mature, easy API, but slightly larger and with more allocation behavior to reason about.
  - jsmn: extremely small and allocation-light; requires a bit more glue code.

## Asset Decoding

### Decision: Start with “SDL-native” formats, add PNG later

- Decision:
  - Early milestones: use **WAV** via `SDL_LoadWAV` and **BMP** via `SDL_LoadBMP` (no extra decoder dependencies).
  - Later: add PNG decoding when textures/sprites demand it (via `SDL_image` or a tiny vendored decoder).
- Rationale:
  - Minimizes dependencies and keeps early progress unblocked.
  - Aligns with the plan’s priority: fast iteration and boring builds.
- Alternatives considered:
  - SDL_image from day one: convenient, but adds an external dependency and packaging complexity.
  - stb_image: vendor-friendly, but larger; acceptable later if it proves the easiest boring option.

## Rendering Approach

### Decision: Sector-based 2.5D map with raycasting + floorcasting

- Decision: Use a **sector + linedef** map model (Doom-like heights, no stacked rooms) rendered via software raycasting.
- Rationale:
  - Supports varied floor/ceiling heights while respecting “no arches / no stacked rooms”.
  - Editor-friendly (explicit vertices/walls/sectors).
- Alternatives considered:
  - Wolf3D grid: simpler, but fights against the “Doom-like floor heights” requirement.

### Decision: Framebuffer-first software renderer

- Decision: Render to an internal CPU framebuffer (e.g., 320×200, 640×400, 800×600) then scale to the window.
- Rationale:
  - Makes rendering deterministic and easy to debug.
  - Integer scaling supports crisp retro visuals.
- Alternatives considered:
  - Rendering directly at window resolution: complicates tuning and hurts predictable performance.

## Rendering Improvements (Depth, HUD, Lighting)

### Decision: Fix sprite occlusion by sorting billboards far→near

- Decision: Before drawing billboards, collect visible entities into a small array and sort by perpendicular distance (`perp`) descending (furthest first).
- Rationale:
  - Current wall depth buffer only solves wall-vs-sprite occlusion; sprite-vs-sprite overlap depends on draw order.
  - Sorting far→near is the simplest classic raycaster fix and remains readable.
- Alternatives considered:
  - Full per-pixel Z buffer: more correctness but higher cost/complexity.
  - Per-column sprite depth buffer: good follow-up improvement, but sorting is the minimal first step.

### Decision: Doom-style bottom status bar HUD (labels + numbers)

- Decision: Replace debug text HUD with a fixed bottom bar that displays labeled numbers for `HP`, `MORTUM`, `AMMO`, and `KEYS`.
- Rationale:
  - Player immediately understands what the numbers mean (labelled).
  - Bottom bar keeps the action area clear and matches the intended retro style.
- Alternatives considered:
  - Floating text at top-left: minimal but ambiguous and visually noisy.
  - Icon-only HUD: would require new art assets; out of scope.

### Decision: Simple lighting model = distance falloff × sector light × tint + optional point lights

- Decision:
  - Keep existing distance-based darkening, but implement as true color multiplication (tint-aware).
  - Multiply by sector light intensity (existing `Sector.light`) and a new sector tint (RGB).
  - Add optional point lights (small list) that brighten locally with distance attenuation.
- Rationale:
  - Achieves “illusion of lighting” without expensive shading.
  - Sector tint gives immediate atmosphere with trivial runtime cost.
  - Point lights allow “light source” moments (torches/lamps) while keeping format simple.
- Alternatives considered:
  - Real-time dynamic lighting/shadows: too complex for current renderer.
  - Lightmaps: needs additional tools/content pipeline.

### Decision: Spatial queries start brute-force, then upgrade

- Decision:
  - Early: intersect rays against all walls (brute force).
  - Soon: add a simple acceleration structure (uniform grid buckets or BSP-like partition).
- Rationale:
  - Keeps milestone 1–3 readable and shippable.
  - Avoids premature complexity until maps grow.
- Alternatives considered:
  - BSP immediately: useful long-term but heavier complexity up front.

## Simulation + Game Loop

### Decision: Fixed timestep simulation at 60Hz

- Decision: Use a fixed timestep simulation (target 60Hz) with an accumulator.
- Rationale:
  - Consistent movement/combat feel independent of frame rate.
  - Simplifies testability and tuning.
- Alternatives considered:
  - Variable timestep: simpler initially, but tends to create jittery feel and tuning headaches.

## Memory/Ownership Discipline (C)

### Decision: Explicit init/destroy everywhere + clear ownership in APIs

- Decision: Use explicit init/destroy pairs for resources and keep ownership obvious (no “who frees this?” ambiguity).
- Rationale:
  - Directly implements constitution requirements (no raw leaks; every allocation has an owner).
- Alternatives considered:
  - Ad-hoc allocation/free: faster to write, but error-prone and harder to debug.
