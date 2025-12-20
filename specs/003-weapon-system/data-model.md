# Phase 1 Design — Data Model (Weapon Viewmodels)

## Runtime Structures

### WeaponId (enum)

Extend existing `WeaponId` to represent the supported set:

- `WEAPON_HANDGUN`
- `WEAPON_SHOTGUN`
- `WEAPON_RIFLE`
- `WEAPON_SMG`
- `WEAPON_ROCKET`

Constraints:
- Weapon IDs are contiguous `[0, WEAPON_COUNT)`.
- Bitmask ownership uses `1u << (unsigned)id`.

### WeaponDef

Existing gameplay definition (ammo/cooldown/projectile tuning). Proposed additions only if needed by visuals:

- `float view_kick_px` (optional)
- `float anim_fps` (optional)

Constraints:
- A shot is “successful” if ammo is consumed and at least one projectile is spawned.

### WeaponVisual

Static mapping from `WeaponId` → asset directory and sprite paths (all under `Assets/Images/`):

Fields:
- `dir_name`: string (one of: `Handgun`, `Shotgun`, `Rifle`, `SMG`, `Rocket`)
- `icon_path`: `Weapons/<dir_name>/<WEAPON>-ICON.png`
- `pickup_path`: `Weapons/<dir_name>/<WEAPON>-PICKUP.png`
- `idle_path`: `Weapons/<dir_name>/<WEAPON>-IDLE.png`
- `shoot_paths[6]`: `Weapons/<dir_name>/<WEAPON>-SHOOT-1.png` … `-SHOOT-6.png`

Validation:
- Paths must be non-empty.
- Shoot frame count is exactly 6.

### WeaponViewState

Per-player runtime state for first-person weapon rendering.

Fields:
- `weapon_id`: `WeaponId` (current equipped)
- `bob_phase`: float (radians)
- `bob_amp`: float (0..1) (smoothed toward movement speed)
- `anim_state`: enum (`idle`, `shooting`)
- `anim_frame`: int (0..5)
- `anim_t`: float (seconds accumulated for frame stepping)

State transitions:
- `idle → shooting` when a shot is successfully fired.
- `shooting → idle` when the last shoot frame has displayed for its frame duration.

### Player additions

To drive sway without cross-module coupling, store velocity in `Player`:

- `float vx, vy` (units/sec)

Constraints:
- `vx,vy` updated in `player_controller_update`.

## Rendering/Draw Contracts

- Weapon viewmodel is drawn in screen space.
- Weapon viewmodel is drawn **before** HUD.
- Weapon icon is drawn inside HUD.
- PNG sprites require alpha blending.

## Map/Entity Contracts

- Weapon pickups are standard entities using string types:
  - `pickup_weapon_handgun`
  - `pickup_weapon_shotgun`
  - `pickup_weapon_rifle`
  - `pickup_weapon_smg`
  - `pickup_weapon_rocket`

Pickup effect:
- Adds weapon ownership bit.
- Equips the weapon.
- Grants some starter ammo (type-specific).
