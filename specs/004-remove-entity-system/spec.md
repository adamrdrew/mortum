# Feature Specification: Remove Entity System

**Feature Branch**: `004-remove-entity-system`  
**Created**: 2025-12-22  
**Status**: Draft  
**Input**: User description: "Remove all code that deals with entities. Remove any entity-only files/headers. The project must still build and run, and non-entity features must still work."

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

### User Story 1 - Keep the game playable after refactor (Priority: P1)

As a player, I can launch the game and play a level without crashes or obvious regressions, even after the internal entity system has been removed.

**Why this priority**: This refactor must not break the game; keeping the executable usable is the minimum safety bar.

**Independent Test**: A manual smoke test that launches the game, loads a level, moves, uses core interactions, and exits successfully.

**Acceptance Scenarios**:

1. **Given** a clean build output, **When** the game is launched, **Then** it reaches the main menu (or first interactive screen) without crashing.
2. **Given** the game is running, **When** a level is started, **Then** the player can move and interact with the world and return/exit without a crash.

---

### User Story 2 - Remove entity-system surface area (Priority: P2)

As a developer, I can work in the codebase without any remaining entity-system module, types, or public interfaces, so later systems can be built without inheriting that abstraction.

**Why this priority**: Leaving a partially-removed system creates ongoing confusion, duplicate patterns, and hidden dependencies.

**Independent Test**: Review the public interface and project structure to confirm there is no entity-system API or entity-only module remaining.

**Acceptance Scenarios**:

1. **Given** the refactor is complete, **When** a developer scans the project’s public headers and source layout, **Then** there is no entity-system API exposed for use.
2. **Given** the refactor is complete, **When** the game is built from a clean state using the repository’s standard build steps, **Then** compilation succeeds without requiring any entity-system code.

---

### User Story 3 - Preserve non-entity features (Priority: P3)

As a player, the systems that are not inherently about entities (e.g., level loading, rendering, input, audio, UI/HUD, and core weapon handling) continue to behave as they did before the refactor.

**Why this priority**: The refactor is meant to unblock later work, not to reduce existing functionality.

**Independent Test**: A focused regression pass through non-entity functionality (load levels, render world, play audio, operate menus/HUD, use a weapon).

**Acceptance Scenarios**:

1. **Given** the game is running, **When** the player starts a level, **Then** the world renders correctly and input controls respond as expected.
2. **Given** the game is running, **When** typical audio-triggering actions occur, **Then** audio playback functions and does not crash or hang.

---

[Add more user stories as needed, each with an assigned priority]

### Edge Cases

- Levels or scripted flows that previously spawned or referenced entity-driven content must not crash; missing content is handled gracefully.
- Debug-only flows (if present) must not crash when attempting to access removed entity-related functionality; they should fail safely.
- The game must remain stable when transitioning between states (menu → level → menu, restart level, exit).

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The project MUST build successfully using the repository’s standard build process from a clean state.
- **FR-002**: The built game MUST launch and reach an interactive state (menu or first interactive screen) without crashing.
- **FR-003**: Starting a level MUST succeed and the player MUST be able to move and interact with the world.
- **FR-004**: The entity system MUST be fully removed such that the codebase no longer exposes or relies on entity-system concepts as a public interface.
- **FR-005**: Any source files and headers that exist solely to implement the entity system MUST be removed from the repository.
- **FR-006**: Any code paths that previously depended on entities MUST either be removed or changed so the game remains stable and does not crash.
- **FR-007**: Non-entity features (rendering, input, level loading, audio, UI/HUD, and weapon handling) MUST continue to function after the refactor.
- **FR-008**: The refactor MUST preserve the existing user-facing flow for launching and starting gameplay (no new required steps for the player).

## Success Criteria *(mandatory)*

<!--
  ACTION REQUIRED: Define measurable success criteria.
  These must be technology-agnostic and measurable.
-->

### Measurable Outcomes

- **SC-001**: A clean build completes successfully with no manual intervention beyond the documented standard steps.
- **SC-002**: The game can be launched and reaches an interactive state in under 10 seconds on a typical developer machine.
- **SC-003**: A 10-minute manual smoke test (menu → start level → move/interact → exit) completes with zero crashes.
- **SC-004**: At least two different levels can be started in the same session without a crash (including transitioning back to menu between them).
