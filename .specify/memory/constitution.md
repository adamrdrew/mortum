<!--
Sync Impact Report

- Version change: N/A (template) → 1.0.0
- Modified principles: N/A (template) →
	- Added: I. Readability Beats Cleverness
	- Added: II. Sandi Metz Rules (Adapted for C)
	- Added: III. Memory Safety Is a Feature
	- Added: IV. Cross-Platform by Default
	- Added: V. Boring Builds, Fool-proof Docs
- Added sections:
	- Non-Goals & Iron Laws
	- Architecture & Engineering Standards
- Removed sections: N/A (template)
- Templates requiring updates:
	- ✅ .specify/templates/plan-template.md
	- ✅ .specify/templates/tasks-template.md
	- ✅ .specify/templates/spec-template.md (no changes needed)
	- ✅ .specify/templates/checklist-template.md (no changes needed)
	- ✅ .specify/templates/agent-file-template.md (no changes needed)
- Follow-up TODOs: None
-->

# Mortum Constitution

Build a small, old-school Doom/Wolf3D-style shooter in C that is:

- Easy to build (minimal deps, one command, good errors)
- Memory-safe by discipline (careful ownership, no leaks)
- Readable and intelligible (clarity over cleverness)
- Cross-platform (macOS, Linux, Windows)

This is recreational programming. The goal is joy + craft, not benchmarks.

## Core Principles

### I. Readability Beats Cleverness

Non-negotiables:

- Prefer the simplest implementation that is correct and easy to understand.
- Optimize only when profiling demonstrates it matters, and keep optimizations
	legible.
- Avoid “smart” macros, tricky pointer arithmetic, and opaque bit hacks unless
	they are the simplest correct option.

Rationale: readable C stays correct longer and is easier to debug.

### II. Sandi Metz Rules (Adapted for C)

Non-negotiables:

- Functions are short and do one thing.
- Names are honest: the name should tell you what the code does.
- Keep fan-out low: avoid mega-functions that “know everything.”
- Prefer composition over deep conditional pyramids; use small helpers and
	explicit flows.
- A change should have one obvious place to go.

Rationale: small, composable code reduces accidental complexity.

### III. Memory Safety Is a Feature

C doesn’t forgive you. Our codebase should.

Non-negotiables:

- No casual `malloc`/`free`; allocations must follow clear conventions.
- Ownership and lifetimes MUST be obvious from APIs and types.
- Debug builds MUST catch mistakes early (assertions, sanitizer-friendly design).

Rationale: disciplined memory ownership is the foundation of stability.

### IV. Cross-Platform by Default

Non-negotiables:

- No platform-specific code in gameplay modules.
- Platform details live behind a narrow `platform` API boundary.
- If `#ifdef` is required, isolate it to the smallest number of files and keep
	it out of gameplay logic.

Rationale: portability is easier when enforced from day one.

### V. Boring Builds, Fool-proof Docs

Non-negotiables:

- A clean checkout MUST build with one command and good error messages.
- Dependencies MUST be minimal and easy to install or vendor.
- Docs MUST be written for a tired human and still succeed.

Rationale: friction kills momentum; “boring” is a feature.

## Non-Goals & Iron Laws

### Non-Goals (Explicit)

- Not a full Doom clone.
- Not a high-performance renderer project.
- Not an engine meant to be reused by others.
- Not dependent on heavy frameworks or complex toolchains.

### Iron Laws (Must Not Be Violated)

#### A) No undefined behavior, knowingly

- Compile with high warnings; treat warnings as errors in dev/CI.
- Prefer safe standard library usage; avoid UB-prone patterns.
- Assert preconditions in debug builds.

#### B) Every allocation has an owner

- Every heap allocation MUST have a clear owner responsible for freeing it.
- Ownership MUST be documented at the API/type level.
- No “who frees this?” ambiguity.

#### C) No raw resource leaks

- Files, window handles, textures, audio buffers, etc. MUST have matching
	destroy/close calls.
- Prefer explicit init/destroy pairs.

#### D) Build must remain simple

- If a dependency makes builds painful, we do not take it.
- Vendoring a small library is acceptable if it improves reliability.
- The Makefile is a first-class deliverable.

#### E) Modules have boundaries

- Rendering code MUST NOT reach into gameplay state directly.
- Gameplay MUST NOT call platform APIs directly.
- Communicate through explicit interfaces.

## Architecture & Engineering Standards

### Architecture Principles

#### 1) “Object-oriented C” style

We emulate OO with:

- `struct` for state
- functions that take `Thing* self`
- explicit init/destroy
- minimal global state

Example pattern:

```c
typedef struct Renderer Renderer;

bool renderer_init(Renderer* self, const RendererConfig* cfg);
void renderer_destroy(Renderer* self);

void renderer_begin_frame(Renderer* self);
void renderer_draw_world(Renderer* self, const World* world, const Camera* cam);
void renderer_end_frame(Renderer* self);
```

#### 2) Prefer data + small operations

- Keep structs plain.
- Keep functions small and explicit.
- Prefer returning status (`bool`, `enum Result`) over magic.

#### 3) Dependency direction

High-level gameplay depends on interfaces, not implementations:

- game → world, input, audio, render
- platform implements window/input/time/files
- render depends on platform only through a narrow shim

### Coding Standards

#### File and module rules

- One module = one responsibility.
- Modules expose a small header: `module.h`.
- Private helpers live in `module.c` (or `module_internal.h` only when
	necessary).

#### Naming

- Types: PascalCase (`World`, `Player`)
- Functions: `snake_case` with module prefix (`world_init`)
- Constants/macros: SCREAMING_SNAKE_CASE (use sparingly)

#### Error handling

- No silent failures.
- If a function can fail, it returns a status and reports errors consistently.

#### Assertions

- Use assertions for programmer errors.
- Use error returns for runtime failures.

### Memory Management Rules

#### Allocation policy

- Prefer stack allocation for small, short-lived values.
- Prefer arenas/pools for “many small things” created and destroyed together.
- Avoid per-frame heap churn.

#### Ownership conventions

Every API MUST document:

- Who owns it
- Who frees it
- How long it remains valid

Conventions:

- `create_*()` returns an owned pointer; caller frees
- `*_init(self)` / `*_destroy(self)` for embedded structs
- Borrowed pointers are valid only for the duration of the call unless stated
	otherwise

Allowed patterns:

- init/destroy pairs
- single cleanup block using `goto cleanup;`
- minimal, obvious helper macros only

### Build & Tooling Constitution

#### Toolchains

- Clang (primary), GCC (secondary)
- Target standard: C11

#### Makefile guarantees

- `make` → debug build
- `make release` → optimized build
- `make run` → run the game
- `make test` → run tests
- `make clean` → works safely
- Clear errors for missing dependencies

#### Dependencies

- Prefer one cross-platform library for window/input/audio.
- All dependencies MUST be easy to install or vendored.

### Documentation Rules

Docs MUST let someone build the project half-asleep.

Required:

- `README.md`
- `docs/ARCHITECTURE.md`
- `docs/CONTRIBUTING.md`

### Testing & Debuggability

- Favor deterministic logic.
- Write small tests for math, parsing, and rules.
- Prefer pure functions for testable logic.
- Debug overlays and logging are features.

Logging:

- Central logging module with levels
- No scattered `printf`

### Source Control & Change Discipline

- Small commits, clear messages.
- Every change keeps the build green.
- No drive-by refactors without payoff.

### Decision Checklist

Before adding code or a dependency, ask:

1. Does this improve clarity?
2. Does it preserve cross-platform builds?
3. Are ownership and lifetimes obvious?
4. Can a tired human understand it in 60 seconds?
5. Does it keep builds and docs boring?

If not, redesign or don’t do it.

## Governance

- This constitution supersedes local conventions and personal preferences.
- All PRs and reviews MUST verify compliance with:
	- readability and simplicity
	- memory/resource ownership rules
	- “no UB knowingly” (warnings, assertions, sanitizer-friendly practices)
	- cross-platform boundaries
	- build/doc simplicity
- Amendments are made via PR that includes:
	- the rationale (what pain it solves)
	- expected impact (what code patterns change)
	- migration notes if existing code must be updated
- Versioning policy (SemVer):
	- MAJOR: principles removed/redefined or governance made stricter in a
		backward-incompatible way
	- MINOR: new principle/section added, or guidance materially expanded
	- PATCH: clarifications, wording improvements, typo fixes

**Version**: 1.0.0 | **Ratified**: 2025-12-19 | **Last Amended**: 2025-12-19
