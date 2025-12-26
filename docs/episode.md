# Mortum Episode System

This document describes **how the Mortum episode system works end-to-end**, including:

- The **Episode JSON format** (required fields, defaults, and what is/ isn’t validated).
- The runtime data model (`Episode`, `EpisodeRunner`).
- How episodes integrate into startup, the main loop, and level transitions.
- The concrete extension points to add episode features (branching, metadata, splash screens, etc.).

It is written to be **LLM/developer-agent friendly**: explicit schemas, lifetimes/ownership rules, and the exact engine entry points that implement behavior.

---

## Source of truth (read these first)

The authoritative implementations for episodes are:

- Loading/parsing: `episode_load()` in `include/assets/episode_loader.h` implemented by `src/assets/episode_loader.c`
- Runtime progression: `EpisodeRunner` helpers in `include/game/episode_runner.h` implemented by `src/game/episode_runner.c`

Primary integration points:

- Startup selection of the initial map (default episode vs explicit map arg): `src/main.c`
- Episode advancement on win edge: `src/main.c`
- Developer console commands (`load_episode`, `load_map`): `src/game/console_commands.c`

There are also spec artifacts you may see referenced:

- JSON Schema contract: `specs/001-mortum-vision-spec/contracts/episode.schema.json` (currently matches the engine loader)

---

## Quick concepts / glossary

- **Episode file**: a JSON file under `Assets/Episodes/` (example: `Assets/Episodes/episode1.json`).
- **Episode**: metadata + an **ordered list of map filenames**.
- **Map filename**: a string like `"e1m1.json"` that is treated as **relative to `Assets/Levels/`** when the engine loads it.
- **EpisodeRunner**: holds a single integer index (`map_index`) into `Episode.maps[]`.

The current engine behavior is intentionally minimal: episodes are linear and have no branching logic.

---

## Episode JSON format

### Location and resolution

- Episodes are loaded from `Assets/Episodes/<episode_filename>`.
- The engine stores each `maps[]` entry as a string, and later loads maps from `Assets/Levels/<map_filename>`.

### Required fields

The loader requires exactly these top-level keys:

- `name` (string)
- `splash` (string)
- `maps` (non-empty array of strings)

If any are missing or have the wrong type, `episode_load()` returns `false`.

**Important:**

- The loader does **not** validate that `splash` refers to a real image file.
- The loader does **not** validate that the map filenames exist; they are validated/loaded later (during map loading).
- The loader does **not** enforce `additionalProperties: false` at runtime; any extra keys in the JSON object are simply ignored.

### Example

`Assets/Episodes/episode1.json`:

```json
{
  "name": "Episode 1: The Spill",
  "splash": "episode1_splash.bmp",
  "maps": ["e1m1.json", "arena.json"]
}
```

### Filename safety rules (practical constraints)

The loader itself will accept any string as an episode filename or map filename; it simply concatenates paths under `Assets/`.

However, the **developer console** deliberately restricts `load_episode`/`load_map` arguments via `name_is_safe_filename()` in `src/game/console_commands.c`:

- Disallows `".."` anywhere (prevents traversal)
- Disallows `/` and `\\` (prevents subdirectories)
- Allows only `[A-Za-z0-9_.-]`

If you are adding UI/content selection that takes user-typed names, you should apply the same constraints.

---

## Runtime data model

### `Episode` (loaded asset)

Defined in `include/assets/episode_loader.h`:

```c
typedef struct Episode {
    char* name;   // owned
    char* splash; // owned
    char** maps;  // owned array of owned strings
    int map_count;
} Episode;
```

Ownership rules:

- All strings (`name`, `splash`, each `maps[i]`) are heap-allocated and owned by the `Episode`.
- You must call `episode_destroy()` when you are done with the episode.

### `EpisodeRunner` (progress state)

Defined in `include/game/episode_runner.h`:

```c
typedef struct EpisodeRunner {
    int map_index;
} EpisodeRunner;
```

Key methods:

- `episode_runner_init(EpisodeRunner*)` sets `map_index = 0`.
- `episode_runner_start(EpisodeRunner*, const Episode*)` starts the episode at the first map.
- `episode_runner_current_map(const EpisodeRunner*, const Episode*) -> const char*` returns `ep->maps[map_index]` or `NULL`.
- `episode_runner_advance(EpisodeRunner*, const Episode*) -> bool` increments `map_index` if a next map exists.

---

## Loader API and lifetimes

### `bool episode_load(Episode* out, const AssetPaths* paths, const char* episode_filename)`

Behavior (from `src/assets/episode_loader.c`):

- **Overwrites** the output struct: it begins with `memset(out, 0, sizeof(*out))`.
- Loads JSON from `asset_path_join(paths, "Episodes", episode_filename)`.
- Requires the JSON root to be an object.
- Requires `name`, `splash`, `maps` keys; validates types.
- Requires `maps` to be a non-empty array.
- Duplicates strings into owned allocations.

Important lifecycle note:

- Because `episode_load()` overwrites the struct, if `*out` already owns allocations from a previous episode you must `episode_destroy(out)` before calling `episode_load()`.
- The console does this explicitly before reloading an episode.

### `void episode_destroy(Episode* self)`

Frees all owned memory and resets the struct to a clean state (`NULL` pointers, `map_count = 0`).

---

## End-to-end integration in the game

### Startup selection logic

At startup (`src/main.c`):

- If `--scene <scene.json>` is used, the game runs in **standalone scene mode**:
  - Episodes and maps are not loaded.
- Otherwise, the engine attempts to load the default episode:
  - `episode_load(&ep, &paths, cfg->content.default_episode)`
- If a map filename argument is provided (e.g. `mortum_test.json`), it takes precedence:
  - `map_name_buf = map_name_arg`
- Else if the episode loaded successfully and `episode_runner_start()` succeeds:
  - `using_episode = true`
  - `map_name_buf = episode_runner_current_map()` (first map)

Then, if `map_name_buf` is non-empty, the engine loads the map via `map_load()`.

### Console: loading episodes and maps

The console command handler (`src/game/console_commands.c`) includes:

- `load_episode <episode_filename>`:
  - Validates the name with `name_is_safe_filename()`.
  - Calls `episode_destroy()` then `episode_load()`.
  - Resets and starts the runner.
  - Sets `using_episode = true`.
  - Loads the first map of the episode.

- `load_map <map_filename>`:
  - Validates the name with `name_is_safe_filename()`.
  - Calls `map_load()`.
  - Resets mesh/player/entities/emitters appropriately.
  - Stops episode progression if invoked in “stop episode” mode (the map-loading helper can clear `using_episode`).

### Episode progression on win

During the main loop (`src/main.c`), episode progression occurs on the **win edge**:

- Detects transition into win mode: `win_now = (gs.mode == GAME_MODE_WIN)` and `win_now && !win_prev`.
- If `using_episode` is true:
  - Calls `episode_runner_advance(&runner, &ep)`.
  - If a next map exists:
    - Destroys the previous `MapLoadResult` and rebuilds the `LevelMesh`.
    - Loads the next map via `map_load()`.
    - Applies per-level start resets via `episode_runner_apply_level_start(&player, &map)`.
    - Respawns map-authored emitters and entities.
    - Switches back into gameplay: `gs.mode = GAME_MODE_PLAYING`.
    - Updates background music via `maybe_start_map_music(...)`.

**End of episode:** if `episode_runner_advance()` returns `false` (no next map), the game does not automatically change state; it remains in win mode unless other logic changes it.

### Per-level reset semantics

`episode_runner_apply_level_start(Player*, const MapLoadResult*)` resets player position/velocity and per-level state while keeping longer-term progression.

Notably, it:

- Sets player `(x,y)` and yaw from the map’s `player_start_*`.
- Finds an appropriate spawn sector (prefers the highest floor with enough headroom if multiple sectors overlap).
- Resets per-level bits like keys, cooldown timers, and some session state.

---

## Asset validation workflow

There is a small asset validation tool in `tools/validate_assets.c` (wired into `make validate`).

Behavior:

- With no args: validates `episode1.json` and then attempts to load every map listed in that episode.
- With args: treats each argument as a map filename and validates it directly.

Examples:

- Validate default episode and its maps:
  - `make validate`
- Validate a specific map:
  - `make validate MAP=arena.json`

Note: this tool currently does not validate `Episode.splash`.

---

## Extension points (common evolutions)

The current episode system is minimal; if you want to extend it, these are the usual touch points:

- Add new episode JSON fields:
  - Update `src/assets/episode_loader.c` to parse/store them (and update `Episode` in `include/assets/episode_loader.h`).
  - Decide whether unknown fields should be ignored (current behavior) or rejected.

- Use `splash` for UI:
  - Today `Episode.splash` is loaded but not consumed elsewhere.
  - A typical integration is a new screen or overlay that loads the image and displays it when an episode starts or ends.

- Non-linear progression (branching / hubs / unlocks):
  - `EpisodeRunner` currently only supports an integer `map_index`.
  - You would extend `EpisodeRunner` to track richer state (node ids, flags), and adjust the win-edge transition logic in `src/main.c` accordingly.

- Persisting episode progress:
  - Today, progress is in-memory only.
  - You could serialize `EpisodeRunner` (and any added state) in a save system, or into config.

---

## Gotchas / correctness notes

- `episode_load()` overwrites the output struct. Destroy old content first when reloading into an existing `Episode`.
- `maps[]` must be non-empty or the load fails.
- Console commands restrict filenames; config/CLI paths are not similarly sanitized.
- `splash` is currently metadata-only (loaded, owned, but unused by runtime code).
