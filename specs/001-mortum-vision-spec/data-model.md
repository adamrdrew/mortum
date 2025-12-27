# Phase 1 Design — Data Model (Mortum)

This describes the minimum set of entities and file-backed data needed to satisfy the feature spec acceptance scenarios.

## Runtime Entities

### PlayerState

Represents the player’s persistent state within an episode/run.

Fields:
- `health_current`: number
- `health_max`: number
- `mortum_pct`: number (0..100)
- `ammo`: map of `AmmoType -> int`
- `weapons_owned`: set of `WeaponId`
- `weapon_equipped`: `WeaponId`
- `upgrades`: `Upgrades`
- `inventory`: `Inventory` (keys, purge items)
- `position`: `Vec2` (x,y)
- `z`: number (standing height relative to sector floor)
- `velocity`: `Vec2`
- `facing_angle_deg`: number
- `state`: enum (`normal`, `undead`)

Validation:
- `mortum_pct` is clamped to [0,100].
- `health_current <= health_max`.

### Weapon

Defines weapon behavior and ammo usage.

Fields:
- `id`: string
- `name`: string
- `role`: enum (`sidearm`, `close_burst`, `precise_mid`, `crowd_control`)
- `ammo_type`: `AmmoType | null` (null for infinite/sidearm if desired)
- `fire_mode`: enum (`hitscan`, `projectile`)
- `cadence_rps`: number (shots per second)
- `spread_deg`: number
- `range_units`: number
- `projectile_spec`: optional `ProjectileSpec` (when `fire_mode=projectile`)

### Enemy

Represents an enemy instance in the world.

Fields:
- `type`: string
- `category`: enum (`melee`, `ranged`, `turret`, `elite`, `boss`)
- `health_current`: number
- `health_max`: number
- `position`: `Vec2`
- `z`: number
- `ai_state`: enum (implementation-defined)
- `attacks`: list of `AttackSpec`
- `drops`: list of `DropSpec`

Behavior requirements mapping:
- Melee enemies apply pressure by closing distance (FR-004).
- Ranged/turret enemies use learnable patterns with a tell (FR-005).

### Projectile

Lightweight entity spawned by enemies/weapons.

Fields:
- `position`: `Vec2`
- `z`: number
- `velocity`: `Vec2`
- `radius`: number
- `damage`: number
- `source`: enum (`player`, `enemy`)
- `lifetime_s`: number
- `pattern_tag`: optional string (for debugging/telemetry)

### Pickup

Point entity that modifies player state when collected/used.

Fields:
- `type`: enum (`health`, `ammo`, `key`, `purge_item`, `purge_shard`, `upgrade_max_health`, `upgrade_max_ammo`)
- `position`: `Vec2`
- `value`: number | object (type-specific)

Rules mapping:
- Purge items reduce Mortum immediately and predictably (FR-008).
- Purge shards contribute to Undead recovery progress (FR-011/012).

### Gate

Simple gating mechanisms that block progress until conditions are met.

Minimal representation:
- `id`: string
- `type`: enum (`key_door`, `switch_barrier`)
- `condition`: object (e.g., `{ "requires_key": "red" }` or `{ "requires_switch": "s1" }`)
- `target`: reference to a door/wall barrier entity

### UndeadModeSession

Tracks the “crisis phase” when Mortum reaches 100%.

Fields:
- `active`: bool
- `health_drain_per_s`: number (FR-010)
- `shards_required`: int
- `shards_collected`: int
- `spawn_aggression_multiplier`: number (implementation-defined)

State transitions:
- `normal -> undead`: when `mortum_pct == 100` (FR-009)
- `undead -> normal`: when `shards_collected >= shards_required` (FR-011/012)

## World/Level Representation

### Map (Level)

A map is a 2.5D sector + wall graph.

Core components:

#### Vertex
- `x`: number
- `y`: number

#### Sector
- `id`: int
- `floor_z`: number
- `ceil_z`: number
- `floor_tex`: string
- `ceil_tex`: string
- `light`: number (0..1)
- `light_color`: RGB tint (each 0..1), optional; default white

Validation:
- `ceil_z > floor_z`.

#### PointLight (optional)

Minimal representation for “illusion of light sources” without a heavy renderer.

- `x`: number
- `y`: number
- `z`: number (optional; default 0)
- `radius`: number (> 0)
- `intensity`: number (>= 0)
- `color`: RGB tint (each 0..1), optional; default white

#### Wall (Linedef)
- `v0`: int (index into `vertices`)
- `v1`: int (index into `vertices`)
- `front_sector`: int (sector id)
- `back_sector`: int (sector id) or `-1` for one-sided
- `tex`: string
- `flags`: list of strings (e.g., `solid`, `door`, `trigger`)

Validation:
- `v0 != v1`.
- `front_sector` exists.
- `back_sector == -1` or exists.

#### PlayerStart
- `x`: number
- `y`: number
- `angle_deg`: number

#### Entity Placement
Map-defined spawns for enemies/pickups/gates.

## File-backed Data

### Episode File

Minimal JSON file listing levels for an episode.

Fields:
- `name`: string
- `splash`: string (path relative to `Assets/Images/`)
- `maps`: array of strings (paths relative to `Assets/Levels/`)

### Map File

Minimal JSON file containing:
- metadata (`version`, `name`)
- `player_start`
- `textures` (optional; may be inferred by scanning referenced strings)
- `vertices`, `sectors`, `walls`
- `entities` (spawn points)

Schema contracts for these files live in `contracts/`.
