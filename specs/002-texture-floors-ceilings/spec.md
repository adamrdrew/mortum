# Feature Specification: Texture Floors/Ceilings + New Texture Library

**Feature Branch**: `002-texture-floors-ceilings`  
**Created**: 2025-12-20  
**Status**: Draft  
**Input**: Improve texture handling: use `Assets/Images/Textures/*` textures (64x64), update example maps to reference them, and render textured floors/ceilings per-sector.

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

### User Story 1 - Textured floors & ceilings (Priority: P1)

As a player, I want floors and ceilings to be textured (not flat colors) so spaces look richer and easier to read.

**Why this priority**: This is the core visual upgrade; it immediately improves moment-to-moment feel.

**Independent Test**: Launch the game and load an existing map with a sector that specifies `floor_tex`/`ceil_tex`; verify the floor and ceiling render using those textures.

**Acceptance Scenarios**:

1. **Given** a map sector specifying `floor_tex` and `ceil_tex`, **When** the map is rendered, **Then** the floor and ceiling are drawn using the corresponding textures.
2. **Given** multiple sectors with different `floor_tex`/`ceil_tex`, **When** the player looks across sector boundaries, **Then** the floor/ceiling appearance matches the sector being viewed (no single global floor/ceiling color).

---

### User Story 2 - Maps can reference the new texture library (Priority: P2)

As a developer/mapper, I want to reference any filename inside `Assets/Images/Textures/` for walls/floors/ceilings so maps can use the expanded texture set.

**Why this priority**: Enables better visuals without custom builds or per-map asset hacks.

**Independent Test**: Update one wall texture in an example map to a known texture filename from `Assets/Images/Textures/`; load the map and verify it appears and no missing-texture error is logged.

**Acceptance Scenarios**:

1. **Given** a map referencing `Assets/Images/Textures/BRICK_1A.PNG` (by filename), **When** the map is loaded, **Then** the engine successfully loads the texture and uses it on the wall.
2. **Given** a map referencing an unknown texture filename, **When** the map is loaded/rendered, **Then** the engine reports the missing asset clearly and uses a visible fallback texture/color (no crash).

---

### User Story 3 - Example maps showcase textures (Priority: P3)

As a developer, I want the repository’s example maps to use the new texture set so the out-of-the-box demo looks good.

**Why this priority**: Keeps the repo “first run” experience strong and serves as living documentation for map authors.

**Independent Test**: Load each example map and verify walls, floors, and ceilings are textured and all referenced textures exist.

**Acceptance Scenarios**:

1. **Given** the updated example maps in `Assets/Levels/`, **When** each map is loaded, **Then** no texture loads fail and the scene renders with varied wall/floor/ceiling textures.

---

[Add more user stories as needed, each with an assigned priority]

### Edge Cases

<!--
  ACTION REQUIRED: The content in this section represents placeholders.
  Fill them out with the right edge cases.
-->

- Map references a texture that exists but is the wrong size (not 64x64).
- Map references a texture with unsupported format/extension.
- Sector omits `floor_tex`/`ceil_tex` (should be rejected by validation and/or treated as a clear error).
- A texture filename is longer than the engine’s fixed buffers (e.g., >63 chars).

## Requirements *(mandatory)*

<!--
  ACTION REQUIRED: The content in this section represents placeholders.
  Fill them out with the right functional requirements.
-->

### Functional Requirements

- **FR-001**: The engine MUST load wall/floor/ceiling textures by filename from `Assets/Images/Textures/`.
- **FR-002**: The texture loader MUST support the texture files present in `Assets/Images/Textures/` (currently `.PNG`, all 64x64).
- **FR-003**: Each map sector MUST specify `floor_tex` and `ceil_tex`, and the renderer MUST use them for that sector.
- **FR-004**: The renderer MUST draw textured floors and ceilings (not flat-color fills).
- **FR-005**: The example maps under `Assets/Levels/` MUST be updated to reference textures from `Assets/Images/Textures/`.
- **FR-006**: Missing/invalid textures MUST not crash the engine; failures MUST produce clear log output and a visible fallback.

### Key Entities *(include if feature involves data)*

- **Texture Asset**: A 64x64 image referenced by filename (walls, floor_tex, ceil_tex).
- **Sector**: Contains `floor_z`, `ceil_z`, `floor_tex`, `ceil_tex`, lighting info.
- **Wall**: References a wall texture filename.
- **Map**: Collection of vertices, sectors, walls, entities (and optional lights).

## Success Criteria *(mandatory)*

<!--
  ACTION REQUIRED: Define measurable success criteria.
  These must be technology-agnostic and measurable.
-->

### Measurable Outcomes

- **SC-001**: Loading and rendering `Assets/Levels/arena.json` shows textured walls + textured floor + textured ceiling.
- **SC-002**: All example maps load without missing-texture errors.
- **SC-003**: No regressions in build/test workflow (`make`, `make test` still pass).
