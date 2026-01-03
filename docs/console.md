# In-Game Console (Developer Documentation)

Mortum includes a Quake-style in-game console for debugging, live tuning, and developer commands.

This document describes:
- how the console is implemented and wired into the game
- how commands are registered and dispatched
- how to extend it with new commands

## Goals / UX

- Toggle with the grave/tilde key (`` ` ``).
- Black background with white text.
- Input prompt at the bottom; output scrolls upward.
- Input line shows a blinking underscore cursor at the current position to indicate activity.
- Output is a fixed-size ring buffer; old lines drop off.
- Console opacity is tunable via `console_opacity` in config.json (default 0.9) and can be changed live with the `config_change` command.
- Gameplay input is disabled while the console is open.
- Escape does **not** close the console (Escape is reserved by default for releasing mouse capture when the console is closed).
- Close the console via the `exit` command, the `--close` flag, or the toggle key.

Keyboard behavior (when console is open):
- Up/Down: command history
- Shift+Up/Down: scroll console output
- Enter: run current input line

## Source of truth

- Console public API/state: [include/game/console.h](../include/game/console.h)
- Console implementation (editing, history, scroll, parsing, dispatch): [src/game/console.c](../src/game/console.c)
- Built-in commands and `help`: [include/game/console_commands.h](../include/game/console_commands.h), [src/game/console_commands.c](../src/game/console_commands.c)
- Integration + input routing: [src/main.c](../src/main.c)
- Input system (key-press events + UTF-8 text input): [include/platform/input.h](../include/platform/input.h), [src/platform/input_sdl.c](../src/platform/input_sdl.c)

## High-level architecture

### Console state

The `Console` struct stores:
- open/closed state
- current input line buffer
- output lines (ring buffer)
- registered commands (`ConsoleCommand[]`)
- command history buffer
- scroll offset (how far up you are from the bottom)

Key constants live in [include/game/console.h](../include/game/console.h):
- `CONSOLE_MAX_LINES` (output ring size)
- `CONSOLE_LINE_MAX` (max characters per output line)
- `CONSOLE_MAX_INPUT` (max input line length)
- `CONSOLE_MAX_TOKENS` (max tokens per command line)
- `CONSOLE_HISTORY_MAX` (max history entries)

### Input plumbing

The console relies on two inputs captured per-frame:

- **Key pressed** events (edge-triggered): used for toggles and history/scroll navigation.
- **Text input** (UTF-8): used to append characters to the input line.

SDL text events are used so that character generation is correct for keyboard layouts; this is why gameplay input and console text input are routed separately.

### Update loop integration

At runtime, `src/main.c`:
- toggles console open/closed on grave/tilde key press
- when the console is open:
  - routes input to `console_update(...)`
  - prevents gameplay controls from acting
  - draws the console overlay with `console_draw(...)`

## Command system


### Command registration and flags


Commands are registered with:

- `console_register_command(Console* con, ConsoleCommand cmd)`

#### Command flags

All commands accept flags, which are parsed before command logic. The following flag is supported:

- `--close` — closes the console before the command runs (the command logic still executes)

Flags can appear before or after the command name. Unknown flags produce `Error: Unknown flag: <flag>`.

Flags are listed in the help output. The flag system is extensible for future flags.

The game’s built-in commands are registered by:

- `console_commands_register_all(Console* con)` in [src/game/console_commands.c](../src/game/console_commands.c)

A command is a `ConsoleCommand`:
- `name`: identifier typed by the user
- `description`: one-line help text
- `example`: example usage string (optional)
- `syntax`: syntax string (optional)
- `fn`: callback

The callback signature is:

- `bool (*ConsoleCommandFn)(Console* con, int argc, const char** argv, void* user_ctx)`

### Command context (`user_ctx`)

Commands call into engine systems through a single POD context struct:

- `ConsoleCommandContext` in [include/game/console_commands.h](../include/game/console_commands.h)

`src/main.c` owns and populates this context and passes it into `console_update(...)` as `user_ctx`.

This keeps the console module itself decoupled from gameplay systems, while still allowing commands to control the engine.

### Parsing rules

The console parses a single input line into tokens (up to `CONSOLE_MAX_TOKENS`).
- Whitespace separates tokens.
- Quoted strings are supported.
- A small set of backslash escapes are supported.

If a command name does not match any registered command, the console prints an error.

## Built-in commands

Defined in [src/game/console_commands.c](../src/game/console_commands.c).
- `clear` — clears console output
- `exit` — closes console
- `help` / `help <command>` — lists commands (alphabetically) or shows details
- `config_reload` — reloads config from disk (non-fatal on invalid config)
- `config_change <key_path> <value>` — updates a single config key in memory (validated)
- `load_map <map.json>` — loads a map from `Assets/Levels/`
- `unload_map` — unloads the current map/world
- `load_timeline <timeline.json>` — loads a timeline from `Assets/Timelines/` (safe relative path; unloads current map/world and starts timeline flow)
- `load_scene <scene.json>` — loads a scene from `Assets/Scenes/` (runs as active screen; gameplay suspended while active)
- `dump_perf` — begins a perf trace capture
- `dump_entities` — prints an entity + projection dump into the console
- `show_fps <boolean>` — toggles FPS overlay
- `show_debug <boolean>` — toggles debug overlay
- `show_font_test <boolean>` — toggles font smoke test page
- `noclip` — toggles noclip movement
- `player_reset` — resets player state to new-game defaults (health, ammo, weapons, keys, etc.)
- `enable_light_emitters <boolean>` — toggles point light emitters
- `enable_sound_emitters <boolean>` — toggles SFX emitters
- `enable_music <boolean>` — toggles background music
- `full_screen <boolean>` — toggles fullscreen/windowed (no-arg form toggles)
- `inventory_list` — lists inventory items
- `inventory_add <string>` — adds an inventory item (no duplicates)
- `inventory_remove <string>` — removes an inventory item
- `inventory_contains <string>` — checks if an inventory item exists
- `notify <string>` — shows a toast notification (upper-right)
- `door_list` — lists door IDs in the current map
- `door_open <id>` — opens a door by ID (developer command; requires an active map)

## Console Opacity

- The console's opacity is controlled by the `console_opacity` value in `config.json` (default 0.9).
- You can change the opacity live using the `config_change console_opacity <value>` command.

## Extending the console

### Add a new command

1. Implement the command function in [src/game/console_commands.c](../src/game/console_commands.c):
   - `static bool cmd_my_command(Console* con, int argc, const char** argv, void* user_ctx)`
   - Validate `argc` and print actionable errors via `console_print(con, "Error: ...")`.

2. Register it in `console_commands_register_all(Console* con)`:
   - add a new `console_register_command(...)` entry
   - fill out `description`, and optionally `example` and `syntax`

3. If it needs engine state, add a pointer/field to `ConsoleCommandContext` (owned by main), populate it in `src/main.c`, then read it from `user_ctx`.

### Console module changes

If you need different behavior (more scrollback, more tokens, etc.), adjust constants in [include/game/console.h](../include/game/console.h) and keep the implementation in [src/game/console.c](../src/game/console.c) consistent.

## Notes / gotchas

- The console suppresses the first text-input frame after opening so the toggle key doesn’t insert a character into the input line.
- Submitting a command resets scroll to `0` so the console follows the latest output again.
- The input line always shows a blinking underscore cursor at the current input position.
