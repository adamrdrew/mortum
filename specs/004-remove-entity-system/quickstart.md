# Quickstart: Remove Entity System

## Build
- `make clean`
- `make`

## Run
- `make run`

Optional: run a specific level file:
- `make run MAP=arena.json`

## Smoke Test Checklist (10 minutes)
- Launch reaches interactive state (menu/first playable screen)
- Start/load a level
- Move (WASD) + look (mouse)
- Verify world renders (walls/floors/ceilings)
- Verify HUD still renders
- Use weapon controls (fire / switch weapons) without crash
- Exit cleanly

## Hard Verification Gate

After implementation, the following must return **no matches**:
- Search case-insensitive `entity` across all `.c` and `.h` in `src/` and `include/`.

(Exact command is implementation-defined; any equivalent grep/ripgrep workflow is acceptable.)
