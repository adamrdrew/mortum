# Research: Remove Entity System

## Scope Decisions

### Decision: Remove all runtime “entity” abstraction from C code
- **Chosen**: Delete the `Entity` / `EntityList` module and remove every usage from all `.c`/`.h` files.
- **Rationale**: The requirement is explicit: no trace in any `.c`/`.h` file and a case-insensitive search for `entity` must return zero results. The safest way to guarantee that is to remove the module and its API entirely.
- **Alternatives considered**:
  - Rename `Entity` to another term (e.g., “Actor”). Rejected because it preserves the same abstraction under a new name, which is likely contrary to “remove the entity system” even if it passes a string search.

### Decision: Keep the game playable by minimizing feature coupling
- **Chosen**: Target “playable” as: build → launch → load a level → move/look → render world → basic weapon/viewmodel + HUD still run.
- **Rationale**: Many current gameplay subsystems are tightly coupled through a single “everything bag” list and stringly-typed object categories. Removing that abstraction without a replacement is a large refactor; the plan will sequence changes to avoid regressions.
- **Alternatives considered**:
  - Replace with multiple specialized lists (foes/shots/loot/locks/hazards/finish marker). Feasible but larger; reserve as a follow-up if we must preserve enemy/pickup gameplay in the same pass.

### Decision: Treat map JSON “entities” as out-of-scope unless required
- **Chosen**: Remove C-side requirements to read an `entities` field; leave any existing extra keys in map files untouched unless they break load.
- **Rationale**: The hard requirement is about `.c`/`.h` and documentation. Map files are data, and the loader can simply ignore unknown keys.
- **Alternatives considered**:
  - Rewrite all level JSON to remove the `entities` key. Rejected for now because it’s a wide content change; can be done later as a cleanup pass.

## Documentation Interpretation

### Decision: “Documentation” means user-facing project docs
- **Chosen**: Ensure `README.md` and `docs/*.md` contain no mention of the removed system.
- **Assumption**: Internal tooling templates under `.specify/` and historical specs under `specs/` are not considered “project documentation” for the shipped game.
- **Rationale**: Otherwise this feature would require rewriting the spec system templates (which are generic and not game-specific) and even this feature spec itself, which is self-contradictory.

## Verification Strategy

### Decision: Enforce the string-ban mechanically
- **Chosen**: Add a final verification step that searches `src/**/*.c`, `src/**/*.h`, `include/**/*.h` for case-insensitive `entity` and fails the build if any match remains.
- **Rationale**: The requirement is easy to validate and should be treated as a hard gate.

