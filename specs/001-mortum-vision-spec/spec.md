# Feature Specification: Mortum Game Vision

**Feature Branch**: `001-mortum-vision-spec`  
**Created**: 2025-12-19  
**Status**: Draft  
**Input**: User description: "Mortum — fast, frenetic retro shooter blending key-hunt exploration with bullet-hell dodging and a corruption (Mortum) mechanic that can turn the player undead."

## Clarifications

### Session 2025-12-19

- Q: In Undead mode, how are “purge shards” obtained? → A: Every enemy kill drops exactly 1 purge shard (deterministic).
- Q: Should Mortum (corruption %) persist between levels within an episode/run? → A: Carry Mortum across levels unchanged.
- Q: If Mortum reaches 100% in the same moment the player takes fatal damage, what should happen? → A: Death takes priority (game over).
- Q: If the player reaches the exit while in Undead mode, what should happen? → A: Block exit until the player cleanses; while Undead is active, enemies spawn continuously and escalate in difficulty/intensity over time.
- Q: For MVP Mortum gain sources, which should be considered “in scope”? → A: Both hazard zones and enemy proximity.

## User Scenarios & Testing *(mandatory)*

<!--
  IMPORTANT: User stories should be PRIORITIZED as user journeys ordered by importance.
  Each user story/journey must be INDEPENDENTLY TESTABLE - meaning if you implement just ONE of them,
  you should still have a viable MVP (Minimum Viable Product) that delivers value.
  
  Assign priorities (P1, P2, P3, etc.) to each story, where P1 is the most critical.
  Think of each story as a standalone slice of functionality that can be:
  - Developed independently
  - Tested independently
  - Deployed independently
  - Demonstrated to users independently
-->

### User Story 1 - Complete a Level Run (Priority: P1)

As a player, I can enter a level, navigate spaces, fight enemies, solve simple gates (keys/switches), and reach the exit so that each level feels like a compact, replayable “retro shooter” experience.

**Why this priority**: This is the baseline loop; without it, other mechanics have no context.

**Independent Test**: A single level is playable start-to-exit with at least one gate (key or switch), at least one combat encounter, and basic pickups.

**Acceptance Scenarios**:

1. **Given** a new level start, **When** the player explores and finds the required gate item(s), **Then** the previously-blocked path becomes accessible and the player can progress.
2. **Given** a level with enemies and pickups, **When** the player engages and survives encounters, **Then** ammo/health pickups affect the player state and the level can still be completed.

---

### User Story 2 - Survive Mortum Corruption and Recover (Priority: P2)

As a player, I can monitor Mortum (corruption) as it rises during dangerous play, spend scarce purge items to reduce it, and—if I reach full corruption—enter an “Undead mode” crisis where I must play aggressively to cleanse myself.

**Why this priority**: Mortum is the core differentiator and the feature that creates memorable clutch moments.

**Independent Test**: In a controlled test level/room, Mortum can increase from exposure/hazards, can be reduced via a purge item pickup/use, and reaching 100% triggers Undead mode with a recoverable cleanse objective.

**Acceptance Scenarios**:

1. **Given** Mortum below 100%, **When** the player is exposed to corruption sources, **Then** Mortum increases and is visible to the player.
2. **Given** the player has a purge item, **When** they use it, **Then** Mortum decreases immediately and predictably.
3. **Given** Mortum reaches 100%, **When** Undead mode begins, **Then** the player experiences a time-pressure survival phase (health drains) and can return to normal by collecting the required cleansing pickups.
4. **Given** Undead mode is active, **When** time passes, **Then** enemies spawn continuously and escalate in difficulty/intensity until the player cleanses.

---

### User Story 3 - Make Meaningful Combat Choices (Priority: P3)

As a player, I can choose between distinct weapons and upgrades (max health vs max ammo) so that combat supports different play styles without becoming complicated.

**Why this priority**: Distinct weapon roles and light build flexibility support replayability and “choice matters.”

**Independent Test**: In a sandbox arena, at least four baseline weapons behave differently (range, cadence, crowd control), and at least two upgrade types permanently change the player’s capacity (max health, max ammo).

**Acceptance Scenarios**:

1. **Given** multiple weapons are available, **When** the player switches weapons, **Then** each weapon’s role is apparent through its behavior (e.g., close-range burst vs sustained crowd control).
2. **Given** an upgrade pickup, **When** the player collects it, **Then** the player’s max health or max ammo increases persistently for subsequent gameplay.

---

### Edge Cases

- What happens when Mortum reaches 100% at the same moment the player takes fatal damage? → Death takes priority (player dies).
- What happens when the player enters Undead mode while low on ammo?
- What happens when a purge item is used at (or near) 0% Mortum?
- What happens if the player attempts to pick up ammo while already at max for that ammo type?
- What happens if the player reaches the exit while in Undead mode? → Exit is blocked until the player cleanses.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The game MUST provide levels with a clear primary objective: reach an exit.
- **FR-002**: The game MUST include at least one “simple gate” pattern (e.g., key-locked door, switch-controlled barrier) that blocks progress until the required condition is met.
- **FR-003**: The player MUST have a reliable evasive movement option (e.g., a quick-step/dash) suitable for weaving through projectile gaps.
- **FR-004**: Enemies MUST include a mix of melee pressure (space control) and ranged projectile threats (pattern recognition + weaving).
- **FR-005**: Ranged enemy/turret attacks MUST use consistent, learnable patterns and MUST provide a clear tell before firing.

- **FR-006**: The player MUST have a visible state including health, ammo (by type), and Mortum (0%–100%).
- **FR-007**: Mortum MUST increase due to defined in-world sources.
- **FR-007a**: For MVP, Mortum sources MUST include both (1) hazardous zones and (2) enemy exposure/proximity.
- **FR-008**: The game MUST provide scarce purge items that reduce Mortum when used.
- **FR-009**: When Mortum reaches 100%, the game MUST transition the player into Undead mode (not an immediate fail state).
- **FR-009a**: If the player takes fatal damage at the same moment Mortum would reach 100%, death MUST take priority (no Undead transition).
- **FR-010**: In Undead mode, the player’s health MUST drain over time, creating time pressure.
- **FR-011**: In Undead mode, the game MUST provide a clear recovery path: enemies spawn aggressively and each enemy kill drops exactly 1 cleansing pickup (“purge shard”) required to return to normal.
- **FR-011a**: While Undead mode is active, enemies MUST spawn continuously and MUST escalate in difficulty/intensity over time until the player cleanses.
- **FR-012**: The game MUST clearly communicate Undead mode goals and remaining recovery progress to the player.

- **FR-012a**: The level exit MUST NOT be completable while Undead mode is active; the player MUST cleanse first.

- **FR-013**: The game MUST include distinct baseline weapons with clear roles (reliable sidearm, close-range burst, precise mid-range, sustained crowd control).
- **FR-014**: Weapons MUST be choices rather than strict upgrades (each remains situationally useful).
- **FR-015**: The game MUST include permanent upgrades that increase max health and max ammo, enabling light build flexibility.

- **FR-016**: Completing a level MUST carry the player’s current loadout and earned upgrades into subsequent levels (within the same run/episode).
- **FR-016a**: Completing a level MUST carry the player’s current Mortum percentage into subsequent levels (within the same run/episode).
- **FR-017**: The game MUST support an episodic structure composed of multiple levels and a climactic finale.
- **FR-018**: The game MUST include pickups for health, ammo by type, and gate items (e.g., keys).
- **FR-019**: Boss or set-piece encounters SHOULD exist as high-intensity tests of movement and pattern reading (exact count per episode is out of scope for this spec).

### Key Entities *(include if feature involves data)*

- **Player State**: Health, Mortum percentage, ammo counts by type, current weapons, permanent upgrades.
- **Weapon**: Name, role, ammo type, firing behavior/cadence, effective range.
- **Enemy**: Category (melee/ranged/turret/elite/boss), movement style, attacks/patterns, drops.
- **Projectile Pattern**: Recognizable “shape” and timing that can be learned and dodged.
- **Pickup**: Type (health, ammo, key, purge item, purge shard, upgrade), value/impact on player state.
- **Level**: Layout of spaces, gates, encounters, hazards/corruption zones, exit condition.
- **Episode**: Ordered set of levels and a finale.
- **Undead Mode Session**: Trigger conditions, drain behavior, recovery requirement, completion/failure outcomes.

## Success Criteria *(mandatory)*

<!--
  ACTION REQUIRED: Define measurable success criteria.
  These must be technology-agnostic and measurable.
-->

### Measurable Outcomes

- **SC-001**: In a moderated playtest, at least 80% of new players can finish the first level (start → exit) within 10 minutes without external guidance.
- **SC-002**: In a moderated playtest, at least 80% of players can correctly explain what Mortum is and how to reduce it after a brief tutorial prompt or first exposure.
- **SC-003**: Across a playtest session, at least 60% of players experience at least one Undead mode event and at least 50% of those events end in a successful cleanse (not death), indicating the crisis is recoverable.
- **SC-004**: In a post-playtest survey (5-point scale), at least 70% of participants rate movement responsiveness and dodge readability as 4/5 or higher.
- **SC-005**: In a post-playtest survey (5-point scale), at least 70% of participants rate weapon distinctness as 4/5 or higher.

## Assumptions

- Single-player, level-based progression.
- No armor system; health is the primary survivability resource.
- Purge items and purge shards are intentionally scarce and are meant to drive meaningful choices.
- “Carry forward” means weapons and permanent upgrades persist between levels within an episode/run.
- Mortum percentage persists between levels within an episode/run.

## Out of Scope

- Multiplayer/co-op.
- Detailed level counts, exact weapon list beyond baseline roles, and exact numeric tuning (damage, drain rates, drop rates).
- Story campaign scripting, cutscenes, or dialogue systems.

## Dependencies

- Playtesting access to representative players (new and experienced) to validate readability, movement feel, and Mortum clarity.
- A baseline content slice (at least one complete level plus a sandbox/arena) to run the acceptance scenarios and success-criteria measurements.
