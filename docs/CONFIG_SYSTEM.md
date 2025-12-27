# Mortum Config System (Implementation Guide)

This document is written to be *LLM-agent friendly*: it focuses on concrete file locations, data flow, invariants, and extension steps.

## Goals / Non-goals

**Goals**
- A single JSON config file controls mod-friendly “tweakable” values.
- Config is validated on every attempted load (startup + reload).
- Startup load: invalid config is **fatal** (abort startup with logged reasons).
- Runtime reload: invalid config is **non-fatal** (log reasons and keep previous config).
- Forward-compatible: unknown keys are **warnings**, not errors.
- Asset references in config must point to files that exist on disk (with semantics noted below).

**Non-goals**
- Layered config overlays or per-mod merge logic (there is exactly **one** config file).
- A separate standalone validation API (validation is embedded in the load path).

## Where the config lives

### Code entry points

- Schema / struct definitions: [include/core/config.h](../include/core/config.h)
- Loader + validation logic: [src/core/config.c](../src/core/config.c)
- Startup path resolution + runtime config reload wiring: [src/main.c](../src/main.c)

### Default file

- Fully-populated default config checked in at repo root: [config.json](../config.json)

## Config file discovery (path precedence)

Resolution is performed in `src/main.c`.

Precedence (first match wins):
1. CLI: `--config <path>`
2. CLI: `CONFIG=<path>`
3. Env: `MORTUS_CONFIG=<path>`
4. `$HOME/.mortus/config.json` (only if it exists)
5. `./config.json` (only if it exists)

Important: there is no overlay/merge; one file is selected.

## Runtime model

### Global config object

- `CoreConfig` is stored as a single global instance in `src/core/config.c`.
- `core_config_get()` returns a `const CoreConfig*`.
- Loads are **atomic**: the loader builds a `next` config, validates it fully, then commits by assigning `g_cfg = next`.

### Load modes

`core_config_load_from_file(path, assets, mode)` supports two modes:

- `CONFIG_LOAD_STARTUP`
  - Any validation failure returns `false` and logs `ERROR:` lines.
  - Caller should abort startup.

- `CONFIG_LOAD_RELOAD`
  - Any validation failure returns `false`, logs, and **keeps the previous config**.

### Reload trigger

Runtime reload is done via the in-game console command `config_reload`.

There is no dedicated reload hotkey; debug/developer keybinds were removed in favor of console commands.

Related:
- `config_reload` attempts to re-load the active config file from disk using `CONFIG_LOAD_RELOAD`.
- `config_change` mutates a single config key in-memory (validated and type-checked) without re-reading the file.

## JSON parsing / tokenization

Mortum uses JSMN via the helper wrapper:

- JSON helper API: [include/assets/json.h](../include/assets/json.h)
- Implementation: [src/assets/json.c](../src/assets/json.c)

Key detail: JSMN object token `size` counts **child tokens** (keys + values). For an object, pair-count is typically `size / 2`.

### Unknown-key warnings

The loader intentionally warns on unknown keys (forward-compat). The implementation walks objects and compares keys against an allow-list per section.

## Validation rules (hard requirements)

Validation happens inside `core_config_load_from_file()` and includes:

1. **Schema correctness**
   - Expected objects are objects (`window`, `render`, `audio`, …).
   - Expected fields have correct type.

2. **Type and range correctness**
   - Numeric fields are range-checked.
   - Strings are checked for non-empty when required.

3. **Asset existence checks (on-disk)**
   - Episode file:
   - Timeline file:
   - `content.boot_timeline` (if non-empty) must exist under `Assets/Timelines/`.
   - `content.boot_episode` is deprecated but may still be accepted for compatibility.
   - WAV assets (only when `audio.enabled` is `true`):
     - Weapon shot WAV files must exist under `Assets/Sounds/Effects/`.
     - A representative footstep WAV is validated using `footsteps.filename_pattern` with variant `1`.

## Asset root semantics

Asset paths are resolved relative to the binary’s base directory:

- `AssetPaths` is initialized from the platform’s base path.
- The assets root is effectively `<base>/Assets`.

This keeps validation and asset loading consistent across `./build/...` and other run layouts.

## What is reloadable vs startup-only

The engine can reload the config file at runtime, but **not every option can safely reinitialize live subsystems**.

General rules:

**Startup-only (restart required)**
- Window creation parameters: `window.width`, `window.height`, `window.vsync`, `window.title`
- Internal framebuffer size: `render.internal_width`, `render.internal_height`
- Audio device open parameters: `audio.sfx_device_freq`, `audio.sfx_device_buffer_samples`
- UI font initialization: `ui.font.file`, `ui.font.size`, `ui.font.atlas_size`

**Reloadable (takes effect immediately after reload)**
- Most gameplay tuning, lighting parameters, keybinds, footsteps, weapon balance, etc.
- `window.grab_mouse` and `window.relative_mouse` are applied immediately.
- SFX master volume is applied immediately.

Implementation detail: `src/main.c` applies some settings immediately after reload and logs a warning that some settings are startup-only.

## Current schema overview

The authoritative schema is `CoreConfig` in [include/core/config.h](../include/core/config.h).

High-level sections:
- `window.*`
- `render.*` and `render.lighting.*`
- `audio.*`
- `content.*`
- `ui.*`
- `input.bindings.*`
- `player.*`
- `footsteps.*`
- `weapons.balance.*`, `weapons.view.*`, `weapons.sfx.*`

The complete set of keys + defaults is mirrored in [config.json](../config.json).

## How to add a new config option (extension checklist)

Use this checklist for future development so new options are consistent, validated, and mod-friendly.

1. **Decide scope + reload semantics**
   - Is it safe to change during gameplay?
     - If yes: reloadable.
     - If no: startup-only; update the reload warning string and documentation.

2. **Add field(s) to `CoreConfig`**
   - Edit [include/core/config.h](../include/core/config.h).
   - Prefer grouping under an existing section (e.g. `render.lighting`, `audio`, `player`, `weapons`).

3. **Set default(s) in `g_cfg`**
   - Edit `static CoreConfig g_cfg = { ... }` in [src/core/config.c](../src/core/config.c).
   - Defaults should preserve current behavior.

4. **Parse the JSON field**
   - In [src/core/config.c](../src/core/config.c), add:
     - Allow-list entry for unknown-key warnings.
     - `json_object_get(...)` for the new key.
     - Type checks + range checks.

5. **Add validation constraints**
   - Range constraints (min/max) should be enforced on load.
   - If it references an asset filename, add a file existence check:
     - Use `validate_asset_file(assets, "<Subdir>", "<Filename>", ...)`.
     - Gate audio-only assets behind `if (next.audio.enabled)`.

6. **Wire it into runtime code**
   - Replace the hard-coded constant with a config read.
   - For reloadable settings, read `core_config_get()` each time or on reload.
     - Pattern used in several subsystems: call `core_config_get()` at point-of-use.

7. **Update `config.json`**
   - Add the new key with the default value.
   - Keep `config.json` “maximally decked out”: if it’s supported, it belongs here.

8. **Update docs**
   - Add the key to the configuration table in [README.md](../README.md).

9. **Sanity test**
   - Start the game.
   - Deliberately break the config (wrong type / out of range / missing asset) and confirm:
     - Startup load aborts.
     - Reload keeps previous config and continues running.

## Patterns used in the codebase

### Atomic commit pattern

Loader builds `CoreConfig next = g_cfg;` then mutates only parsed fields. If any validation fails, it returns without modifying `g_cfg`.

### Keybind parsing

Bindings accept either:
- an integer scancode
- a string key name that SDL understands (via `SDL_GetScancodeFromName`)
- for movement-style binds: a single key OR `[primary, secondary]`

### Audio asset validation gating

Because some users may disable audio entirely, the loader only requires WAV files to exist when `audio.enabled == true`.

## Common pitfalls / gotchas

- **JSMN object size**: object token `size` is keys+values; pair-count is `size/2`.
- **Startup-only changes**: changing window size, internal resolution, vsync, or audio device params won’t take effect after reload.
- **Asset paths**: always validate assets relative to the binary asset root (not CWD).

## Suggested future refactors (optional)

Not required for correctness, but helpful if the config continues to grow:
- Consider generating the README table from the schema to reduce drift.
- Consider a small helper layer for “parse + range check + assign” to reduce repetition.
