# Feature Specification: Weapon Viewmodels, Icons, and Switching

**Feature Branch**: `003-weapon-system`  
**Created**: 2025-12-20  
**Status**: Draft  
**Input**: User description: "Implement most of the weapon system. Weapon assets under Assets/Images/Weapons/{Handgun,Rifle,Rocket,Shotgun,SMG}. Each directory contains WEAPON-ICON.png (HUD), WEAPON-PICKUP.png (map pickup), WEAPON-IDLE.png (viewmodel idle), WEAPON-SHOOT-1..6.png (viewmodel shooting). Weapon is rendered behind the HUD with bottom slightly obscured. Weapon sways while walking. Shooting runs animation. Equipped weapon icon is shown in HUD. Number keys select specific weapons. Q/E cycle weapons."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - See current weapon in HUD and hands (Priority: P1)

As a player, I can see the equipped weapon in the HUD and as a first-person weapon sprite so that combat feedback is readable.

**Why this priority**: Without the viewmodel + icon, switching weapons is confusing and feels unfinished.

**Independent Test**: Start `arena.json`, pick up a weapon, switch weapons, and verify the HUD icon and viewmodel update.

**Acceptance Scenarios**:

1. **Given** the player has multiple weapons, **When** the player switches weapons, **Then** the HUD shows the equipped weapon’s icon and the viewmodel sprite changes to that weapon.
2. **Given** the HUD is visible, **When** the viewmodel is drawn, **Then** the weapon sprite is rendered behind the HUD and its bottom edge is slightly obscured by the HUD bar.

---

### User Story 2 - Weapon sways while moving (Priority: P2)

As a player, when I walk, the weapon sprite sways so the first-person presentation feels alive.

**Why this priority**: The game otherwise feels static even when movement is responsive.

**Independent Test**: Walk forward/strafe in any map and observe periodic sway; stop moving and confirm the weapon settles.

**Acceptance Scenarios**:

1. **Given** the player is moving, **When** the weapon is rendered, **Then** its position shifts slightly in a smooth loop (sway/bob).
2. **Given** the player stops moving, **When** the weapon is rendered, **Then** sway amplitude decays back toward idle.

---

### User Story 3 - Switching and firing plays correct animation (Priority: P3)

As a player, I can select a specific weapon with number keys, cycle with Q/E, and see the shooting animation play when firing.

**Why this priority**: Weapon feel and responsiveness depend on immediate visual feedback.

**Independent Test**: In `arena.json`, press number keys to select (when owned), press Q/E to cycle, and fire to observe SHOOT-1..6 sequence.

**Acceptance Scenarios**:

1. **Given** the player owns multiple weapons, **When** the player presses 1–5, **Then** the equipped weapon becomes that specific weapon (if owned).
2. **Given** the player owns multiple weapons, **When** the player presses Q or E, **Then** the equipped weapon cycles backward/forward through owned weapons.
3. **Given** the player fires a weapon, **When** a shot is successfully fired, **Then** the viewmodel plays the SHOOT animation frames and returns to IDLE afterward.

### Edge Cases

- Missing weapon PNG assets: game continues; draws nothing (or a placeholder) and logs a single load failure without spamming every frame.
- Selecting a weapon not owned: selection is ignored; equipped weapon unchanged.
- Weapon has no ammo: firing does not start the shoot animation.
- Very small framebuffer: weapon still draws; HUD remains readable; weapon may be clipped but should not crash.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST load weapon images from `Assets/Images/Weapons/<WeaponDir>/` for supported weapons.
- **FR-002**: System MUST render the equipped weapon viewmodel behind the HUD, with the HUD obscuring the bottom few pixels.
- **FR-003**: System MUST sway the weapon viewmodel when the player walks.
- **FR-004**: System MUST play the shooting animation cycle `WEAPON-SHOOT-1..6.png` when a shot is successfully fired.
- **FR-005**: System MUST show the equipped weapon icon (`WEAPON-ICON.png`) in the HUD.
- **FR-006**: System MUST support selecting specific weapons via number keys (1–5).
- **FR-007**: System MUST support cycling owned weapons via Q/E keys.
- **FR-008**: System MUST render weapon pickups in the world using `WEAPON-PICKUP.png` for each weapon.

### Key Entities *(include if feature involves data)*

- **WeaponId**: Identifies weapons supported by gameplay and visuals.
- **WeaponVisual**: Mapping from `WeaponId` to asset directory + icon/pickup/viewmodel frame paths.
- **WeaponViewState**: Runtime state for viewmodel sway + current animation frame.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In `arena.json`, switching weapons always updates HUD icon and viewmodel within 1 frame.
- **SC-002**: When moving continuously for 5 seconds, sway is visible and smooth (no stutter or large jumps).
- **SC-003**: When firing, the shoot animation plays and returns to idle reliably; no crashes or repeated asset-load spam.
