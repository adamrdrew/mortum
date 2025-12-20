# Phase 0 Research — Weapon Viewmodels, Icons, and Switching

## What exists in the codebase (baseline)

- Weapons already exist as gameplay logic:
  - `WeaponId` currently has 4 entries (`WEAPON_SIDEARM`, `WEAPON_SHOTGUN`, `WEAPON_RIFLE`, `WEAPON_CROWDCONTROL`) in [include/game/weapon_defs.h](include/game/weapon_defs.h#L1).
  - Switching via number keys (1–4) + mouse wheel is implemented in [src/game/weapons.c](src/game/weapons.c#L32).
  - Weapon pickups exist (`pickup_weapon_shotgun`, `pickup_weapon_rifle`, `pickup_weapon_chaingun`) and grant ownership/ammo in [src/game/pickups.c](src/game/pickups.c#L34).
- Rendering:
  - HUD currently draws text-only panels in [src/game/hud.c](src/game/hud.c#L29).
  - World pickups render using a `sprites.bmp` atlas in [src/render/entities.c](src/render/entities.c#L71).
  - Texture loading supports PNG and BMP and caches by filename in [src/render/texture.c](src/render/texture.c#L1), but it currently enforces 64x64 for any `.png`.
- Input:
  - Main loop already builds a weapon selection mask for number keys and passes mouse wheel delta into `weapons_update` in [src/main.c](src/main.c#L186).
  - `E` is currently used for “use purge item” in [src/main.c](src/main.c#L237).

## Decisions

### Decision: Represent weapon assets as normal textures under `Assets/Images/Weapons/...`

- We will reference weapon images via the existing `TextureRegistry` by passing a relative path like `"Weapons/Handgun/HANDGUN-IDLE.png"` (i.e., relative to `Assets/Images/`).
- Rationale: `TextureRegistry` already loads from `Assets/Images/<filename>` as a fallback. By including slashes in the “filename”, the existing path join logic naturally targets `Assets/Images/Weapons/...`.
- Alternatives considered:
  - Add a new image loader path just for weapons. Rejected: duplicates caching + ownership patterns.

### Decision: Relax the “64x64 PNG” enforcement to only apply to world textures

- We will update `TextureRegistry`’s 64x64 validation to only enforce when the PNG is a world texture (e.g., filename has no path separators), allowing weapon PNGs to be any size.
- Rationale: weapon sprites/icons are not 64x64 in general; enforcing 64x64 for all `.png` would break weapon assets.
- Alternatives considered:
  - Keep enforcement and resize weapon sprites. Rejected: shifts burden to art pipeline.

### Decision: Add alpha-blended blit for PNG sprites

- We will add a new draw helper (or extend existing) to alpha-blend RGBA/ABGR pixels into the framebuffer.
- Rationale: weapon PNGs and icons almost certainly rely on transparency; current `draw_blit_rgba8888` overwrites destination pixels.
- Alternatives considered:
  - Pre-multiply and treat a colorkey. Rejected: not robust for PNG alpha.

### Decision: Weapon cycling uses Q/E; purge-item key is re-bound

- We will implement weapon cycling on Q (previous) and E (next) as requested.
- The existing purge-item action bound to E will be moved to another key (tentatively `F`) to avoid conflict.
- Rationale: direct conflict otherwise.
- Alternatives considered:
  - Require a modifier for cycling (e.g., Shift+E). Rejected: not specified.
  - Keep E as purge-item and only implement Q for cycling. Rejected: doesn’t meet requirement.

### Decision: Extend weapons from 4 → 5 and map to new supported set

- Supported set per request: Handgun, Rifle, Rocket, Shotgun, SMG.
- Implementation mapping:
  - Existing `WEAPON_SIDEARM` becomes Handgun.
  - Existing `WEAPON_CROWDCONTROL` becomes SMG (renamed).
  - Add new `WEAPON_ROCKET`.
- Rationale: closest match to current gameplay structure.
- Alternatives considered:
  - Keep “Chaingun” and add “SMG” separately. Rejected: exceeds requested supported set.

## Open Questions (resolved for this plan)

- Sprite sizes: we treat weapon sprites as “draw at native pixel size” (no scaling) to keep implementation simple and readable.
- Viewmodel position: draw centered at bottom, with a small overlap under the HUD (e.g., ~6 px).
- Shoot animation timing: fixed per-frame timestep (e.g., 1/30s) or derive from weapon cooldown; we will implement fixed timestep for consistent feel across weapons.
