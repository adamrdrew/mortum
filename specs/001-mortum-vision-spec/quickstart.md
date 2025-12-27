# Quickstart — Mortum

This document describes the current “boring build” workflow.

## Prerequisites

- A C compiler:
  - macOS: Xcode Command Line Tools (`clang`)
  - Linux: `clang` or `gcc`
  - Windows: MinGW-w64 (recommended for Makefile compatibility)
- SDL2 development libraries:
  - macOS (Homebrew): `brew install sdl2`
  - Linux (Debian/Ubuntu): `sudo apt-get install libsdl2-dev`
  - Windows: use SDL2 development package + MinGW-w64 toolchain (documented when code lands)

## Build + Run (target workflow)

From the repository root:

- Debug build: `make`
- Run: `make run`
- Release build: `make release`
- Tests: `make test`
- Validate assets (offline): `make validate`
- Clean: `make clean`

## Running specific maps

- Default (episode): `make run`
- Override map (relative to `Assets/Levels/`): `make run RUN_MAP=arena.json` (or `./build/mortum arena.json`)

## Controls (current)

- Move: WASD
- Look: mouse (relative mode)
- Fire: left mouse
- Dash: Shift
- Use purge item: E
- Weapon select: 1–4 and mouse wheel

Dev toggles:

- Toggle noclip: F2
- Toggle debug overlay: F3
- Spawn melee enemy: F6

## Assets Layout (target workflow)

- `Assets/Episodes/` — episode JSON files
- `Assets/Levels/` — map JSON files
- `Assets/Images/` — textures/sprites/UI images
- `Assets/Sounds/` — WAV (early); OGG later if desired

## File Format Contracts

- Episode schema: `specs/001-mortum-vision-spec/contracts/episode.schema.json`
- Map schema: `specs/001-mortum-vision-spec/contracts/map.schema.json`
- Map lighting extension: `specs/001-mortum-vision-spec/contracts/map.lighting.schema.json`

## Visual Sanity Checks (after implementation)

- Depth: stand behind a pickup/enemy; nearer sprites should always appear in front of farther sprites.
- HUD: a Doom-style bottom status bar shows labeled values for HP, Mortum, Ammo, and Keys.
- Lighting: far walls/enemies are visibly darker; sectors can be tinted (e.g., greenish) and optional point lights brighten locally.

## Notes

- SDL2 flags are discovered via `sdl2-config` (preferred) or `pkg-config`.
