# Data Model: Remove Entity System

This feature removes the `Entity` / `EntityList` abstraction from the runtime codebase.

## Data Structures Affected

### Removed
- `Entity` (type, position/velocity, combat fields)
- `EntityList` (dynamic array ownership)

### Updated (high level)
- `MapLoadResult` (from [include/assets/map_loader.h](include/assets/map_loader.h))
  - Remove the “things list” field that currently stores dynamic objects.
  - Keep:
    - `World world`
    - `player_start_x/y/angle_deg`
    - background music metadata (`bgmusic`, `soundfont`)

## World and Player

This feature does not change the static world model:
- `World` remains geometry + lights (vertices, sectors, walls, point lights)

This feature does not change the core player state model:
- `Player` remains movement, view angle, loadout/upgrades, HUD state, etc.

## Runtime Behavior After Removal

By design, any runtime systems that previously depended on the removed abstraction must either:
- be deleted (if they exist solely to operate on the removed abstraction), or
- be refactored to operate on non-removed state (e.g., world/player-only systems).

The acceptance bar is that the game builds, launches, loads levels, and core non-removed systems continue functioning.
