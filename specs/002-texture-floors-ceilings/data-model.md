# Data Model — Texture Floors/Ceilings + New Texture Library

This feature is primarily an asset + rendering change. The “data model” is the map JSON contract and the in-memory `World` representation.

## Entities

### Texture Asset

- **Key**: `filename` (string)
- **Source**: `Assets/Images/Textures/<filename>` (preferred), optional fallback `Assets/Images/<filename>`
- **Constraints**:
  - Expected size: 64×64 pixels
  - Expected format: PNG (current library)

### Map (JSON)

- **Fields**:
  - `version`: integer (`1`)
  - `name`: string
  - `player_start`: `{x, y, angle_deg}`
  - `vertices`: array of `{x, y}`
  - `sectors`: array of `Sector`
  - `walls`: array of `Wall`
  - `entities`: array of entity objects
  - `lights`: optional point lights
  - `textures`: optional pre-declaration lists (historical / informational; currently not required by loader)

### Sector

- **Fields**:
  - `id`: integer
  - `floor_z`: number
  - `floor_z_toggled_pos`: optional number
  - `movable`: optional boolean
  - `ceil_z`: number
  - `floor_tex`: string (texture filename)
  - `ceil_tex`: string (texture filename)
  - `light`: number `[0..1]`
  - `light_color`: optional `{r,g,b}`

- **In-memory**: `Sector` in `include/game/world.h`
  - `char floor_tex[64]`
  - `char ceil_tex[64]`

### Wall

- **Fields**:
  - `v0`, `v1`: vertex indices
  - `front_sector`, `back_sector`
  - `tex`: string (texture filename)
  - `active_tex`: optional string
  - `toggle_sector`: optional boolean
  - `toggle_sector_id`: optional integer (references a sector by `id`, not array index)
  - `toggle_sector_oneshot`: optional boolean
  - `flags`: array of strings

- **In-memory**: `Wall` in `include/game/world.h`
  - `char tex[64]`

## Validation rules

- Every `Sector` must have non-empty `floor_tex` and `ceil_tex`.
- Every wall must have non-empty `tex`.
- Texture filenames must fit into 63 chars (+ null terminator) in current engine structs.
- If a referenced texture cannot be loaded or has an unexpected size, rendering uses a visible fallback and logs an error.
