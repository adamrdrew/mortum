# Contract: Level Map JSON (Post-Entity-Removal)

This describes the subset of map JSON consumed by the engine after removing the runtime entity system.

## Root object

Required fields:
- `player_start`: object
  - `x`: number
  - `y`: number
  - `angle_deg`: number
- `vertices`: array of `{ "x": number, "y": number }`
- `sectors`: array of sector objects
  - `id`: integer
  - `floor_z`: number
  - `ceil_z`: number
  - `floor_tex`: string
  - `ceil_tex`: string
  - optional lighting fields (if present in current format)
- `walls`: array of wall objects
  - `v0`: integer (vertex index)
  - `v1`: integer (vertex index)
  - `front_sector`: integer
  - `back_sector`: integer (-1 for solid)
  - `tex`: string

Optional fields:
- `lights`: array of light objects (if supported by the current loader)
- `bgmusic`: string (MIDI filename)
- `soundfont`: string (SoundFont filename)

## Unknown keys

Unknown keys are ignored by the loader to allow content to remain forward/backward compatible.
