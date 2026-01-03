
# Mortum Map JSON — LLM Generation Prompt

Use this prompt to generate **valid** Mortum map files (JSON) that load via the engine’s map loader and pass validation.

You must follow the schema and constraints exactly. The engine is strict about required fields and about sector boundaries being closed, and it will reject maps where the player can end up “outside” any sector.

## Output rules (VERY IMPORTANT)

- Output **only** a single JSON object (no Markdown, no commentary).
- Use **JSON numbers** for all numeric values.
- Keep all referenced filenames under **63 characters** (engine buffers are 64 incl. terminator).
- Prefer simple maps: **one closed loop per sector**, no holes, no internal wall fragments.

## Coordinate system and basic concepts

- The map is 2D in $(x,y)$ for layout.
- Each **sector** defines floor/ceiling heights and textures for a region in the $(x,y)$ plane.
- **Walls** are directed edges between vertices and belong to a sector’s boundary via `front_sector`.
- A wall is:
	- **solid** when `back_sector == -1` (blocks collision and sight).
	- a **portal/open boundary** when `back_sector >= 0` (connects two sectors; not solid).

## Top-level JSON schema

The root must be an object.

### Required fields

- `player_start` (object)
- `vertices` (array)
- `sectors` (array)
- `walls` (array)

### Optional fields (used by the game)

- `bgmusic` (string): MIDI filename under `Assets/Sounds/MIDI/`.
	- If missing: treated as empty/disabled.
	- If present but empty string: disabled.
- `soundfont` (string): SoundFont filename under `Assets/Sounds/SoundFonts/`.
	- If missing: defaults to `"hl4mgm.sf2"`.
	- If present but not a JSON string: the engine does not fail the map, but the value ends up empty (the default is not applied).
- `sky` (string): image filename under `Assets/Images/Sky/` (or fallback locations).
	- If missing or empty: no custom sky.
- `lights` (array): optional point lights (see below).
- `sounds` (array): optional map-authored sound emitters (see below).
- `particles` (array): optional map-authored particle emitters (see below).
- `entities` (array): optional entity placements (see below).

### Permitted-but-ignored fields

These may appear in existing maps but are **ignored** by the loader/validator (safe but pointless):

- `version` (number)
- `name` (string)
- `flags` on walls (array of strings), e.g. `"solid"`, `"portal"`

Do not rely on ignored fields for behavior.

## `player_start` (required)

Object with:

- `x` (number): player spawn x
- `y` (number): player spawn y
- `angle_deg` (number): view angle in degrees

Hard requirements:

- The point `(x,y)` **must be inside at least one sector**.
	- Avoid placing it on a wall/edge/vertex (edge cases can be unstable).
	- The engine does **not** require the point to be inside exactly one sector, but overlap makes behavior ambiguous; avoid overlap unless you understand the consequences.

## `vertices` (required)

Array of vertex objects. Each vertex is:

- `x` (number)
- `y` (number)

Hard requirements:

- Must contain at least **3** vertices.
- Vertex indices used by walls must be within `[0, vertices.length-1]`.

Recommended:

- Use modest coordinate magnitudes (e.g. 0–200) to avoid precision issues.

## `sectors` (required)

Array of sector objects.

Important: **Walls reference sectors by sector array index**, not by `id`.

Each sector object has required fields:

- `id` (integer): metadata only; not used for indexing.
	- Recommended: set `id` equal to the sector’s array index.
	- If you use wall `toggle_sector_id`, ensure every sector `id` is unique.
- `floor_z` (number)
- `ceil_z` (number)
- `floor_tex` (string): texture filename
- `ceil_tex` (string): texture filename
- `light` (number): scalar intensity used by rendering

Optional fields:

- `light_color` (object):
	- `r` (number)
	- `g` (number)
	- `b` (number)
	- Recommended range: `0.0`–`1.0` (not strictly enforced, but expected).

- `movable` (boolean): enables sector floor height manipulation.
	- If `true`, the sector’s floor can move between its authored `floor_z` and `floor_z_toggled_pos`.
	- If omitted: treated as `false`.
	- If `true`, `floor_z_toggled_pos` **must** be present.

- `floor_z_toggled_pos` (number): target floor height for the toggled state.
	- If present, it implies the sector is movable (equivalent to `movable: true`).
	- The floor toggles between:
		- **origin** = `floor_z`
		- **toggled** = `floor_z_toggled_pos`

Hard requirements:

- Must contain at least **1** sector.
- For every sector:
	- `ceil_z` must be **greater than** `floor_z`.
	- If the sector is movable, `ceil_z` must be above the *maximum* of `floor_z` and `floor_z_toggled_pos`.
	- `floor_tex` and `ceil_tex` must be **non-empty strings**.
	- The sector must have at least one **closed boundary loop** formed by its walls where `wall.front_sector == this sector index`.

Texture requirements (runtime):

- `floor_tex`, `ceil_tex`, and wall `tex` are loaded like regular textures.
- Textures found under `Assets/Images/Textures/` must be **64x64** (enforced at load).
- Sky textures under `Assets/Images/Sky/` are not size-enforced.

## `walls` (required)

Array of wall objects. Each wall has required fields:

- `v0` (integer): start vertex index
- `v1` (integer): end vertex index
- `front_sector` (integer): sector index that “owns” this directed wall
- `back_sector` (integer):
	- `-1` for solid boundary
	- otherwise a valid sector index `0..sectors.length-1` for a portal
- `tex` (string): wall texture filename (non-empty)

Optional fields (gameplay triggers):

- `end_level` (boolean): if `true`, this wall completes the level when used.
	- Triggering is done in-game by pressing the action key while touching the wall.
	- Implementation detail: this sets `GameState.mode = GAME_MODE_WIN`, which TimelineFlow interprets as “map completed”.
	- **Precedence**: if `end_level` is true, it takes precedence over all other action interactions on that press.
	- If omitted: treated as `false`.

- `toggle_sector` (boolean): if `true`, this wall can be used as an action-trigger.
	- Triggering is done in-game by pressing the action key while touching the wall.
	- If omitted: treated as `false`.

- `toggle_sector_id` (integer): which sector to control **by sector `id`**.
	- If omitted, the wall toggles the player’s current sector.
	- Note: this is intentionally **not** a sector array index.

- `toggle_sector_oneshot` (boolean): if `true`, the wall can only be used once.
	- After the controlled sector reaches `floor_z_toggled_pos`, further uses do nothing.

- `active_tex` (string): optional alternate wall texture to display when the controlled sector is in the toggled position.
	- When the sector is at origin, the wall displays its normal `tex`.

- `toggle_sound` (string): optional WAV filename under `Assets/Sounds/Effects/`.
- `toggle_sound_finish` (string): optional WAV filename under `Assets/Sounds/Effects/`.

Hard requirements (validated):

- Must contain at least **1** wall.
- For each wall:
	- `v0` and `v1` must be valid vertex indices.
	- `v0 != v1` (no zero-length walls).
	- `front_sector` must be within `0..sectors.length-1`.
	- `back_sector` must be either **exactly** `-1` or within `0..sectors.length-1`.
	- `tex` must be a **non-empty** string.
	- `end_level=true` must not be combined with `toggle_sector=true` on the same wall (end_level takes precedence).
	- A door (`doors[].wall_index`) must not refer to a wall with `end_level=true` (end_level takes precedence over door interaction).

### How to build valid sector boundaries (CRITICAL)

The validator treats a sector’s boundary as the undirected graph of edges from walls with `front_sector == sector`.
For a wall-loop to be considered closed:

- Every boundary vertex used by that sector’s boundary loop has **degree 2** within that sector’s boundary edges.
- The boundary must contain at least **3 edges**.

To guarantee validity:

- For each sector, define its polygon boundary as an ordered list of vertex indices `[p0, p1, p2, ..., pN-1]` (N≥3).
- Add one wall for each polygon edge `(pi -> p(i+1))` (wrapping at end).
- For a solid outer edge, set `back_sector: -1`.
- For a shared edge between sectors A and B (a portal), you must add **two** directed walls:
	- Wall 1: `front_sector: A`, `back_sector: B`, vertices `v0: i`, `v1: j`
	- Wall 2: `front_sector: B`, `back_sector: A`, vertices `v0: j`, `v1: i`

This “paired directed wall” rule is important because each sector’s boundary detection uses only `front_sector` walls.

### Preventing “out-of-bounds non-sector” walkable space

The engine collision system only blocks against **solid** walls (`back_sector == -1`). It does not prevent the player from moving into areas that are not inside any sector if you leave gaps.

Therefore:

- Every edge on the outside of the playable space must be a **solid wall** (`back_sector: -1`).
- Portals must connect two sectors along a **shared boundary edge** (paired walls as above).
- Do not leave holes/gaps in any sector boundary loop.
- Avoid overlapping sector polygons (ambiguous “which sector am I in?”).

## `lights` (optional)

If present, `lights` must be an array of objects.

Each light is a **point-light emitter** that affects lighting only (no sprites/particles).

### Required fields

- `x` (number)
- `y` (number)
- `radius` (number)
- One of:
	- `brightness` (number)
	- `intensity` (number) (legacy alias)

### Optional fields

- `z` (number): defaults to `0`.

- `color`:
	- Hex string: `"#RRGGBB"` or `"RRGGBB"` (recommended), OR
	- Legacy object: `{ "r": number, "g": number, "b": number }` where components are expected in `0..1`.
	- If omitted: defaults to white.

- `flicker` (string): one of `"none"`, `"flame"`, `"malfunction"`.
	- If omitted: `"none"`.
	- Flicker is applied at render time and uses a per-emitter seed to avoid synchronized flicker.

- `seed` (integer): optional deterministic seed for flicker/randomness.
	- If omitted: the engine derives a seed from the light’s position/radius so different lights de-sync.

### Validation / recommendations

- `radius` must be non-negative (recommended: `radius > 0`).
- `brightness`/`intensity` must be non-negative.
- Prefer placing lights inside a sector (the validator may warn if a light is not inside any sector).

Note:

- The loader clamps negative `radius` and `brightness`/`intensity` to `0` (it does not reject the map for those values), but you should still author non-negative values.

### Example

```json
"lights": [
	{ "x": 12.0, "y": 8.0, "z": 0.0, "radius": 6.0, "brightness": 1.2, "color": "#FFAA44", "flicker": "flame", "seed": 12345 },
	{ "x": 20.0, "y": 8.0, "radius": 8.0, "intensity": 0.8, "color": "44AAFF", "flicker": "malfunction" },
	{ "x": 16.0, "y": 14.0, "radius": 5.0, "brightness": 0.6 }
]
```

## Global validation rules you MUST satisfy

The engine will reject the map if any of these fail:

- `vertices.length >= 3`
- `sectors.length >= 1`
- `walls.length >= 1`
- Every sector has `ceil_z > floor_z` and non-empty `floor_tex` and `ceil_tex`.
- If a sector is movable, it must specify `floor_z_toggled_pos`, and `ceil_z` must be above the max of its origin/toggled floor.
- Every sector has at least one closed boundary loop (built from its `front_sector` walls).
- Every wall references valid vertex indices, valid `front_sector`, valid `back_sector` (or `-1`), and has non-empty `tex`.
- If a wall specifies `toggle_sector_id`, it must match some sector object’s `id`.
- `player_start` must be inside some sector.
- **All sectors must be reachable** from the player’s starting sector by traversing portal connectivity (any wall where `back_sector != -1` creates adjacency).

If you include optional arrays, they must match the engine’s expected item schema:

- `lights` (if present) must be an array of objects with required fields as described above.
- `sounds` (if present) must be an array of objects with required fields as described below.
- `particles` (if present) must be an array of objects with required fields as described below.
- `entities` (if present) must be an array of objects with required fields as described below.

## `sounds` (optional)

If present, `sounds` must be an array of objects.

Each sound emitter is a definition that the runtime sound system turns into a live emitter.

### Required fields

- `x` (number)
- `y` (number)
- `sound` (string): WAV filename under `Assets/Sounds/Effects/`

### Optional fields

- `loop` (boolean): default `false`
- `spatial` (boolean): default `true`
- `gain` (number): default `1.0`, clamped to `[0,1]`

## `particles` (optional)

If present, `particles` must be an array of objects.

Each particle emitter is a definition that the runtime particle system turns into a live emitter.

### Required fields

- `x` (number)
- `y` (number)
- `particle_life_ms` (integer)
- `emit_interval_ms` (integer)
- `start` (object)
- `end` (object)

### Optional fields

- `z` (number): default `0`
- `offset_jitter` (number): default `0`
- `rotate` (object)
- `image` (string): filename under `Assets/Images/Particles/` (no path)
- `shape` (string): `"circle" | "square"` (used when `image` is empty)

### `start` / `end` keyframes

Each keyframe object must contain:

- `opacity` (number)
- `size` (number)
- `offset` (object): `{ "x": number, "y": number, "z": number }`
- `color` (object):
	- `value` (string): hex `RRGGBB` or `#RRGGBB`
	- `opacity` (number, optional, default `0.0`): blend opacity for image particles

### `rotate` object

- `enabled` (boolean, default `false`)
- `tick` (object, optional):
	- `deg` (number, default `0`)
	- `time_ms` (integer, default `30`)

Runtime note (sanitization):

- The runtime clamps/sanitizes values when creating the emitter (e.g. `particle_life_ms`/`emit_interval_ms` minimum `1`, opacities clamped to `[0,1]`, sizes clamped non-negative).

## `entities` (optional)

If present, `entities` must be an array of objects.

Each entity placement is a spawn point; the runtime entity system decides what to spawn.

### Required fields

- `x` (number)
- `y` (number)

### Preferred fields (current system)

- `def` (string): entity definition name (resolved at runtime against the loaded entity defs from `Assets/Entities/entities_manifest.json`)

### Optional fields

- `yaw_deg` (number): default `0`

### Legacy field

- `type` (string): legacy pre-entity-system field. Only limited mappings exist (currently `"pickup_health"` → `"health_pickup"`).

Behavior notes:

- If `def` is missing and no legacy `type` mapping applies, the placement is kept but treated as inactive by the runtime spawner.
- If a placement has a `def`, it must be located inside some sector (otherwise the map load fails).
- At runtime, if `def` does not exist in the loaded entity defs, it is skipped with a warning (map still loads).

## Generation checklist (do this internally before output)

1. Choose sector count and define sector polygons with vertex indices.
2. Ensure each sector polygon is a simple closed loop (no self-intersections).
3. Emit `vertices` array.
4. Emit `sectors` array; set `id` = index.
5. Emit `walls`:
	 - For every sector polygon edge, add a wall with `front_sector` = sector.
	 - For every portal edge, add the **paired** opposite-direction wall for the neighbor sector.
	 - For every outside edge, set `back_sector: -1`.
6. Place `player_start` strictly inside the intended sector.
7. Ensure every sector has at least one portal path (directly or indirectly) to the player sector.

## Example: one-shot floor toggle switch (snippet)

This is a focused snippet showing only the new fields. Integrate it into a valid map with correct vertices/walls.

```json
{
	"sectors": [
		{
			"id": 5,
			"floor_z": -6,
			"floor_z_toggled_pos": -1,
			"movable": true,
			"ceil_z": 4,
			"floor_tex": "SLIME_1A.PNG",
			"ceil_tex": "TECH_2E.PNG",
			"light": 0.55
		}
	],
	"walls": [
		{
			"v0": 20,
			"v1": 21,
			"front_sector": 5,
			"back_sector": 4,
			"tex": "SWITCH_1B.PNG",
			"active_tex": "SWITCH_1A.PNG",
			"toggle_sector": true,
			"toggle_sector_id": 5,
			"toggle_sector_oneshot": true
		}
	]
}
```

## Minimal valid example (single square sector)

This is a reference for structure only; replace textures/filenames with ones that exist in the repo.

{
	"bgmusic": "",
	"soundfont": "hl4mgm.sf2",
	"sky": "",
	"player_start": {"x": 2.5, "y": 2.5, "angle_deg": 0},
	"vertices": [
		{"x": 0, "y": 0},
		{"x": 5, "y": 0},
		{"x": 5, "y": 5},
		{"x": 0, "y": 5}
	],
	"sectors": [
		{"id": 0, "floor_z": 0, "ceil_z": 4, "floor_tex": "FLOOR_2A.PNG", "ceil_tex": "TECH_1A.PNG", "light": 1.0}
	],
	"walls": [
		{"v0": 0, "v1": 1, "front_sector": 0, "back_sector": -1, "tex": "CONCRETE_2A.PNG"},
		{"v0": 1, "v1": 2, "front_sector": 0, "back_sector": -1, "tex": "CONCRETE_2A.PNG"},
		{"v0": 2, "v1": 3, "front_sector": 0, "back_sector": -1, "tex": "CONCRETE_2A.PNG"},
		{"v0": 3, "v1": 0, "front_sector": 0, "back_sector": -1, "tex": "CONCRETE_2A.PNG"}
	]
}

# Available Textures
ls ./Assets/Images/Textures/
BIGDOOR_1A.PNG  CRATE_1I.PNG    FLOOR_4C.PNG    SIDEWALK_1A.PNG TECH_1G.PNG
BLOOD_1A.PNG    CRATE_1J.PNG    GRASS_1A.PNG    SIDEWALK_1B.PNG TECH_2A.PNG
BLOOD_1B.PNG    CRATE_1K.PNG    GRASS_1B.PNG    SIDEWALK_1C.PNG TECH_2B.PNG
BRICK_1A.PNG    CRATE_1L.PNG    GRASS_1C.PNG    SLIME_1A.PNG    TECH_2C.PNG
BRICK_1B.PNG    CRATE_1M.PNG    GRASS_1D.PNG    SLIME_1B.PNG    TECH_2D.PNG
BRICK_2A.PNG    CRATE_1N.PNG    GRASS_1E.PNG    SLUDGE_1A.PNG   TECH_2E.PNG
BRICK_2B.PNG    CRATE_2A.PNG    GRASS_1F.PNG    STEEL_1A.PNG    TECH_2F.PNG
BRICK_3A.PNG    CRATE_2B.PNG    GRASS_1G.PNG    STEEL_1B.PNG    TECH_3A.PNG
BRICK_3B.PNG    CRATE_2C.PNG    GRASS_2A.PNG    STEEL_1C.PNG    TECH_3B.PNG
BRICK_3C.PNG    CRATE_2D.PNG    GRASS_2B.PNG    STEEL_1D.PNG    TECH_4A.PNG
BRICK_3D.PNG    CRATE_2E.PNG    GRID_1A.PNG     STEEL_2A.PNG    TECH_4B.PNG
BRICK_3E.PNG    CRATE_2F.PNG    GRID_1B.PNG     STEEL_2B.PNG    TECH_4C.PNG
BRICK_4A.PNG    CRATE_2G.PNG    GRID_1C.PNG     STEEL_2C.PNG    TECH_4D.PNG
BRICK_4B.PNG    CRATE_2H.PNG    GRID_2A.PNG     STEEL_2D.PNG    TECH_4E.PNG
BRICK_4C.PNG    CRATE_2I.PNG    GRID_2B.PNG     STEEL_3A.PNG    TECH_4F.PNG
BRICK_4D.PNG    CRATE_2J.PNG    GRID_2C.PNG     STEEL_3B.PNG    TECH_4G.PNG
BRICK_4E.PNG    CRATE_2K.PNG    HEDGE_1A.PNG    STEEL_3C.PNG    TECH_4H.PNG
BRICK_5A.PNG    CRATE_2L.PNG    HEDGE_1B.PNG    STEEL_4A.PNG    TECH_4I.PNG
BRICK_6A.PNG    CRATE_2M.PNG    HEDGE_1C.PNG    STEEL_4B.PNG    TECH_4J.PNG
BRICK_6B.PNG    CRATE_2N.PNG    HEDGE_1D.PNG    STEEL_4C.PNG    TECH_4K.PNG
BRICK_6C.PNG    CRATE_3A.PNG    HEDGE_1E.PNG    STEEL_5A.PNG    TECH_5A.PNG
BRICK_6D.PNG    CRATE_3B.PNG    HEDGE_2A.PNG    STEEL_5B.PNG    TECH_5B.PNG
BRICK_6E.PNG    CRATE_3C.PNG    LAB_1A.PNG      STEP_1A.PNG     TECH_5C.PNG
BRICK_6F.PNG    DIRT_1A.PNG     LAB_1B.PNG      STEP_2A.PNG     TECH_5D.PNG
COBBLES_1A.PNG  DIRT_1B.PNG     LAB_1C.PNG      STUCCO_1A.PNG   TECH_5E.PNG
COBBLES_1B.PNG  DIRT_1C.PNG     LAB_2A.PNG      STUCCO_1B.PNG   TECH_5F.PNG
COBBLES_1C.PNG  DOOR_1A.PNG     LAB_2B.PNG      STUCCO_1C.PNG   TECH_6A.PNG
COBBLES_1D.PNG  DOOR_1B.PNG     LAB_3A.PNG      STUCCO_2A.PNG   TECH_6B.PNG
COBBLES_1E.PNG  DOOR_1C.PNG     LAB_3B.PNG      STUCCO_2B.PNG   TILE_1A.PNG
COBBLES_2A.PNG  DOOR_1D.PNG     LAB_3C.PNG      STUCCO_2C.PNG   TILE_1B.PNG
COBBLES_2B.PNG  DOOR_1E.PNG     LAB_4A.PNG      STUCCO_3A.PNG   TILE_1C.PNG
COBBLES_2C.PNG  DOOR_1F.PNG     LAB_4B.PNG      STUCCO_3B.PNG   TILE_1D.PNG
COBBLES_2D.PNG  DOOR_1G.PNG     LAB_5A.PNG      STUCCO_3C.PNG   TILE_1E.PNG
COBBLES_2E.PNG  DOOR_1H.PNG     LAB_5B.PNG      SUPPORT_1A.PNG  TILE_1F.PNG
COBBLES_3A.PNG  DOOR_2A.PNG     LDOOR_1A.PNG    SUPPORT_1B.PNG  TILE_1G.PNG
COBBLES_3B.PNG  DOOR_2B.PNG     LEDGE_1A.PNG    SUPPORT_1C.PNG  TILE_2A.PNG
COBBLES_3C.PNG  DOOR_2C.PNG     LIGHT_1A.PNG    SUPPORT_1D.PNG  TILE_2B.PNG
COBBLES_4A.PNG  DOOR_2D.PNG     LIGHT_1B.PNG    SUPPORT_1E.PNG  TILE_2C.PNG
COBBLES_4B.PNG  DOOR_2E.PNG     LIGHT_1C.PNG    SUPPORT_2A.PNG  TILE_2D.PNG
COBBLES_4D.PNG  DOOR_2F.PNG     LIGHT_2A.PNG    SUPPORT_2B.PNG  TILE_2E.PNG
CONCRETE_1A.PNG DOOR_2G.PNG     LIGHT_2B.PNG    SUPPORT_2C.PNG  TILE_2F.PNG
CONCRETE_1B.PNG DOOR_2H.PNG     PANEL_1A.PNG    SUPPORT_2D.PNG  TILE_3A.PNG
CONCRETE_1C.PNG DOOR_3A.PNG     PANEL_1B.PNG    SUPPORT_3A.PNG  TILE_3B.PNG
CONCRETE_1D.PNG DOOR_3B.PNG     PANEL_1C.PNG    SUPPORT_3B.PNG  TILE_3C.PNG
CONCRETE_2A.PNG DOOR_4A.PNG     PANEL_2A.PNG    SUPPORT_3C.PNG  TILE_3D.PNG
CONCRETE_2B.PNG DOOR_4B.PNG     PANEL_2B.PNG    SUPPORT_3D.PNG  TILE_3E.PNG
CONCRETE_2C.PNG DOOR_5A.PNG     PANEL_2C.PNG    SUPPORT_4A.PNG  TILE_3F.PNG
CONCRETE_2D.PNG DOOR_5B.PNG     PANEL_2D.PNG    SUPPORT_4B.PNG  TILE_4A.PNG
CONCRETE_2E.PNG DOOR_6A.PNG     PANEL_2E.PNG    SUPPORT_4C.PNG  TILE_4B.PNG
CONCRETE_2F.PNG DOOR_7A.PNG     PANEL_3A.PNG    SUPPORT_4D.PNG  TILE_4C.PNG
CONCRETE_3A.PNG DOOR_7B.PNG     PANEL_3B.PNG    SUPPORT_5A.PNG  TILE_4D.PNG
CONCRETE_3B.PNG DOOR_7C.PNG     PANEL_3C.PNG    SUPPORT_5B.PNG  TILE_4E.PNG
CONCRETE_3C.PNG DOORTRIM_1A.PNG PAPER_1A.PNG    SUPPORT_5C.PNG  TILE_5A.PNG
CONCRETE_3D.PNG DOORTRIM_1B.PNG PAPER_1B.PNG    SUPPORT_5D.PNG  TILE_5B.PNG
CONCRETE_4A.PNG DOORTRIM_1C.PNG PAPER_1C.PNG    SUPPORT_6A.PNG  TILE_5C.PNG
CONCRETE_4B.PNG DOORTRIM_1D.PNG PAPER_1D.PNG    SUPPORT_6B.PNG  TILE_6A.PNG
CONCRETE_4C.PNG DOORTRIM_1E.PNG PAPER_1E.PNG    SUPPORT_6C.PNG  TILE_6B.PNG
CONCRETE_4D.PNG DOORTRIM_1F.PNG PAPER_1F.PNG    SUPPORT_6D.PNG  TILE_6C.PNG
CONCRETE_4E.PNG DOORTRIM_1G.PNG PAPER_1G.PNG    SUPPORT_7A.PNG  TILE_7A.PNG
CONCRETE_5A.PNG DOORTRIM_1H.PNG PAPER_1H.PNG    SUPPORT_7B.PNG  TILE_7B.PNG
CONCRETE_5B.PNG FENCE_1A.PNG    PAPER_1I.PNG    SUPPORT_7C.PNG  TILE_7D.PNG
CONCRETE_5C.PNG FENCE_1B.PNG    PAPER_1J.PNG    SUPPORT_7D.PNG  TILE_7E.PNG
CONCRETE_6A.PNG FENCE_2A.PNG    PAPER_1K.PNG    SWITCH_1A.PNG   TILE_7F.PNG
CONCRETE_6B.PNG FENCE_2B.PNG    PIPES_1A.PNG    SWITCH_1B.PNG   TILE_7G.PNG
CONCRETE_6C.PNG FENCE_2C.PNG    PIPES_1B.PNG    TARMAC_1A.PNG   VENT_1A.PNG
CONCRETE_7A.PNG FENCE_2D.PNG    PIPES_2A.PNG    TARMAC_1B.PNG   VENT_1B.PNG
CONCRETE_7B.PNG FLOOR_1A.PNG    PIPES_2B.PNG    TARMAC_1C.PNG   WARN_1A.PNG
CONCRETE_7C.PNG FLOOR_1B.PNG    PIPES_3A.PNG    TARMAC_2A.PNG   WARN_2A.PNG
CONCRETE_8A.PNG FLOOR_1C.PNG    RDOOR_1A.PNG    TARMAC_2B.PNG   WOOD_1A.PNG
CONCRETE_8B.PNG FLOOR_1D.PNG    RIVET_1A.PNG    TARMAC_2C.PNG   WOOD_1B.PNG
CONSOLE_1A.PNG  FLOOR_2A.PNG    RIVET_1B.PNG    TARMAC_3A.PNG   WOOD_1C.PNG
CONSOLE_1B.PNG  FLOOR_2B.PNG    RIVET_1C.PNG    TARMAC_3B.PNG   WOOD_1D.PNG
CONSOLE_1C.PNG  FLOOR_2C.PNG    RIVET_2A.PNG    TARMAC_4A.PNG   WOOD_1E.PNG
CONSOLE_1D.PNG  FLOOR_2D.PNG    RIVET_2B.PNG    TARMAC_4B.PNG   WOOD_1F.PNG
CRATE_1A.PNG    FLOOR_2E.PNG    RIVET_2C.PNG    TARMAC_4C.PNG   WOOD_1G.PNG
CRATE_1B.PNG    FLOOR_2F.PNG    RIVET_3A.PNG    TARMAC_4D.PNG   WOOD_2A.PNG
CRATE_1C.PNG    FLOOR_2G.PNG    RIVET_3B.PNG    TECH_1A.PNG     WOOD_2B.PNG
CRATE_1D.PNG    FLOOR_3A.PNG    SAND_1A.PNG     TECH_1B.PNG     WOOD_2C.PNG
CRATE_1E.PNG    FLOOR_3B.PNG    SAND_1B.PNG     TECH_1C.PNG     WOOD_2D.PNG
CRATE_1F.PNG    FLOOR_3C.PNG    SAND_2A.PNG     TECH_1D.PNG     WOOD_3A.PNG
CRATE_1G.PNG    FLOOR_4A.PNG    SAND_2B.PNG     TECH_1E.PNG     WOOD_3B.PNG
CRATE_1H.PNG    FLOOR_4B.PNG    SAND_2C.PNG     TECH_1F.PNG

# Available Music
ls ./Assets/Sounds/MIDI/
ACCRETION-DISK.MID
ALIEN-ACTIVITY.MID
BOUGHS-OF-BLOOD-AND-BONE.MID
DESOLATE.MID
HEART-AGAINST-FEATHER.MID
HYDRAULIC-PRESS.MID
LAY-DOWN-THE-GAUNTLET.MID
LOCKDOWN.MID
LOOKING-FOR-SILVER-FINDING-GOLD.MID
MACHINE-REAPER.MID
MALICE.MID
MANIFOLD.MID
MARCH-OF-THE-MARTIANS.MID
MIDI-VER-ONE-TYRANT-TO-DETHRONE-ANOTHER.MID
MYRMIDON.MID
NEBULA.MID
NOT-TRISTANS-BROKEN-MIRROR.MID
NUKESPILL.MID
PORTAL-JUMP.MID
PULVERISE.MID
RABBLE-ROUSER.MID
RIGHT-BEHIND-YOU.MID
SHOWER-OF-GIBS.MID
SIDEREAL-VOYAGE.MID
SLIPSTREAM.MID
STORMING-THE-GATES.MID
SUBTERFUGE.MID
SUN-SHININ.MID
SWIRLING-SANDS.MID
THE-MACHINES-THAT-SPUN-THE-EARTH.MID
THE-STARSEEKER-LOOP.MID
THE-STARSEEKER.MID
TUNGSTEN-CARBIDE.MID
TUNNEL-VISION.MID
XENOSCAPE.MID
YGGDRASIL.MID

# Available Skyboxes
 ls ./Assets/Images/Sky/
green.png       purple.png      red.png         space.png