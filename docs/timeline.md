# Mortum Timeline System

This document describes **how the Mortum timeline system works end-to-end**, including:

- The **Timeline JSON format** (required fields, validation rules).
- The runtime data model (`Timeline`, `TimelineFlow`).
- How timelines integrate into startup, the main loop, and progression edges.
- The concrete integration points in code (config, console, main loop).

It is written to be **LLM/developer-agent friendly**: explicit schemas, lifetimes/ownership rules, and the exact engine entry points that implement behavior.

---

## Source of truth

Authoritative implementations:

- Loading/parsing: `timeline_load()` in `include/assets/timeline_loader.h` implemented by `src/assets/timeline_loader.c`
- Runtime progression: `TimelineFlow` in `include/game/timeline_flow.h` implemented by `src/game/timeline_flow.c`

Primary integration points:

- Startup selection and edge handling: `src/main.c`
- Developer console commands: `src/game/console_commands.c`
- Config key and validation: `src/core/config.c`

---

## Quick concepts / glossary

- **Timeline file**: a JSON file under `Assets/Timelines/` (example: `Assets/Timelines/boot.json`).
- **Timeline**: a list of **events** run sequentially.
- **Event**: one of:
  - `scene`: runs a Scene as a Screen (gameplay update/render suspended while active).
  - `map`: loads a map and runs gameplay as usual.

---

## Timeline JSON format

### Location and resolution

- Timelines are loaded from `Assets/Timelines/<timeline_filename>`.
- `scene` events load from `Assets/Scenes/<name>`.
- `map` events load from `Assets/Levels/<name>`.

### Required fields

Root object fields:

- `name` (string, required)
- `events` (array, required; may be empty)

Each element of `events` is an object with:

- `kind` (string, required): `"scene" | "map"`
- `name` (string, required)
- `on_complete` (string, required): `"advance" | "loop" | "load"`
- `target` (string, required iff `on_complete == "load"`)

### Example

`Assets/Timelines/boot.json`:

```json
{
  "name": "Boot",
  "events": [
    { "kind": "scene", "name": "developer.json", "on_complete": "advance" },
    { "kind": "scene", "name": "engine.json", "on_complete": "advance" },
    { "kind": "scene", "name": "title.json", "on_complete": "advance" },
    { "kind": "map",   "name": "big.json",      "on_complete": "loop" }
  ]
}
```

---

## Path safety rules

Timeline uses the same safe-path rule family used elsewhere in Mortum.

- Timeline filename (`boot_timeline`, console `load_timeline`, and event `target`) must be a **safe relative path**:
  - Must be relative (cannot start with `/` or `\\`)
  - Must not contain `..`
  - Must not contain `\\`
  - Allowed characters: `[A-Za-z0-9_./-]`

- Scene event `name` must be a **safe relative path** (subfolders allowed) under `Assets/Scenes/`.

- Map event `name` preserves the existing console/map restriction: it must be a **safe filename** (no `/` or `\\`) under `Assets/Levels/`.

---

## Runtime semantics

A timeline runs **one event at a time**.

Completion edges:

- **Scene completion**: when the active Screen completes, TimelineFlow applies the current event’s `on_complete`.
- **Map completion**: the existing win edge (`GAME_MODE_WIN` rising edge) is treated as “map completed” and `on_complete` is applied.

`on_complete` actions:

- `advance`: `idx++`
- `loop`: `idx = 0`
- `load`: loads another timeline from `Assets/Timelines/<target>` and starts at `idx = 0`

The engine allows loading the same timeline repeatedly (including self-target). There is no guard against infinite loops; stability and leak-free teardown are required.

---

## Error handling / fallbacks

Timeline is designed to be robust against missing/bad content:

- If a scene fails to load: logs a warning/error and treats it as completed immediately.
- If a map fails to load: logs error and treats it as completed immediately.
- If a target timeline fails to load (`on_complete = load`): logs error and uses a deterministic fallback:
  - advance within the current timeline if possible, otherwise loop to `idx=0`.

---

## Startup and console integration

Startup selection logic (`src/main.c`):

- If `--scene <scene.json>` is provided: standalone scene mode (no timeline/map boot).
- Else if an explicit map filename arg is provided: it takes precedence and disables timeline flow.
- Else: load `content.boot_timeline` and start TimelineFlow.

Console:

- `load_timeline <timeline.json>` loads a timeline from `Assets/Timelines/`, unloading any current map/world and stopping any active Scene screen.
- `load_episode` is kept as a deprecated alias that forwards to `load_timeline`.
