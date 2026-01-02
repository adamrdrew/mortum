# Menu System (Data-Driven JSON)

This document describes Mortum’s Menu system as implemented in the engine (C), including:

- Runtime architecture and API surface area
- JSON data shape (assets)
- Loader validation rules and common failure modes
- Input behavior and screen lifecycle
- Timeline integration (event kind: `menu`)
- Pause-menu integration (Escape during gameplay)

Source-of-truth for behavior is the engine code, primarily:

- [include/assets/menu.h](../include/assets/menu.h)
- [include/assets/menu_loader.h](../include/assets/menu_loader.h)
- [include/game/menu_screen.h](../include/game/menu_screen.h)
- [src/assets/menu_loader.c](../src/assets/menu_loader.c)
- [src/game/menu_screen.c](../src/game/menu_screen.c)
- [include/assets/timeline_loader.h](../include/assets/timeline_loader.h)
- [src/assets/timeline_loader.c](../src/assets/timeline_loader.c)
- [src/game/timeline_flow.c](../src/game/timeline_flow.c)
- [src/main.c](../src/main.c)


## 1) Mental Model

A Menu is:

- A JSON asset loaded from `Assets/Menus/<file>.json`
- Converted into an owned in-memory `MenuAsset`
- Presented as a `Screen` via `menu_screen_create()`

A MenuScreen is a normal Screen:

- It draws a background, then centered text items, and a cursor
- It consumes input (Up/Down/Enter/Escape)
- It returns `SCREEN_RESULT_DONE` to close

Two primary modes exist:

- **Timeline menu** (`invoked_from_timeline=true`): ESC at the root does *not* close the menu.
- **Pause menu** (`invoked_from_timeline=false`): ESC at the root closes the menu.


## 2) Public API Surface

### 2.1 Menu Asset Types

Defined in [include/assets/menu.h](../include/assets/menu.h).

- `MenuRGBA8`: 8-bit RGBA.
- `MenuSfxTheme`: three optional WAV filenames.
- `MenuTheme`: root-only appearance + audio theme.
- `MenuActionKind`: action types (`NONE`, `COMMAND`, `SUBMENU`, `CLOSE`).
- `MenuAction`: action payload.
- `MenuItem`: a labeled selectable item.
- `MenuView`: a menu page (a “view”) with items.
- `MenuAsset`: the fully-loaded menu.

Ownership model:

- All strings in `MenuAsset` are deep-copied (heap-allocated) and owned by the asset.
- Arrays (`views`, `items`, `args`) are owned.
- `menu_asset_destroy()` deterministically frees everything.

### 2.2 Loader

Declared in [include/assets/menu_loader.h](../include/assets/menu_loader.h):

- `bool menu_load(MenuAsset* out, const AssetPaths* paths, const char* menu_file);`

Contract:

- Overwrites `*out` with a fresh zeroed struct.
- Returns `false` on any validation or IO error (and logs why).
- `menu_file` is a relative path under `Assets/Menus/` (must be safe and end with `.json`).

### 2.3 Asset Lifetime Helpers

Declared in [include/assets/menu.h](../include/assets/menu.h):

- `void menu_asset_destroy(MenuAsset* self);`
- `int menu_asset_find_view(const MenuAsset* self, const char* id);`

### 2.4 Menu Screen

Declared in [include/game/menu_screen.h](../include/game/menu_screen.h):

- `Screen* menu_screen_create(MenuAsset asset, bool invoked_from_timeline, ConsoleCommandContext* cmd_ctx);`

Contract:

- Takes ownership of `asset` (move semantics).
- `invoked_from_timeline` controls root-ESC behavior.
- `cmd_ctx` is used to safely queue a deferred console command.


## 3) Asset Locations (Filesystem Layout)

Menu JSON:

- `Assets/Menus/*.json`

Theme assets:

- Background PNG: `Assets/Images/Menus/Backgrounds/<background>.png`
- Cursor PNG: `Assets/Images/Menus/Cursors/<cursor>.png` (optional)
- Font TTF: `Assets/Fonts/<font>.ttf`
- Menu SFX WAVs: `Assets/Sounds/Menus/<wav>.wav` (optional)
- Menu music MIDI: `Assets/Sounds/MIDI/<path>.mid|.midi` (optional)

Notes:

- Background/cursor/font/SFX use “safe filename” rules (no slashes).
- MIDI uses “safe relative path” rules (may contain subdirectories, but must remain safe).


## 4) JSON Data Shape

### 4.1 Top-level

A menu file must have these top-level fields:

```json
{
  "name": "...",
  "theme": { ... },
  "views": { ... }
}
```

Rules:

- Root must be a JSON object.
- `name` must be a non-empty string.
- `theme` must be an object.
- `views` must be an object.
- `views.root` must exist (the root view id is literally the key `"root"`).


### 4.2 Theme

Theme is **root-only**; per-view theme overrides are disallowed.

Required theme fields:

- `background` (string; safe `.png` filename)
- `font` (string; safe `.ttf` filename)
- `text_size` (int; `1..128`)
- `text_color` (string; must be `"#RRGGBB"`)

Optional theme fields:

- `cursor` (string; safe `.png` filename)
- `cursor_render_size` (int; `1..512`; default `32`)
- `music` (string; safe relative `.mid`/`.midi` path)
- `sfx` (object)

Example:

```json
"theme": {
  "background": "main.png",
  "cursor": "cursor.png",
  "cursor_render_size": 64,
  "font": "ProggyClean.ttf",
  "text_size": 16,
  "text_color": "#FFFFFF",
  "sfx": {
    "on_move": "click.wav",
    "on_select": "click.wav",
    "on_back": "click.wav"
  },
  "music": "DESOLATE.MID"
}
```

#### Cursor transparency (colorkey)

Cursor PNG pixels with RGB `FF00FF` (magenta) are treated as fully transparent:

- Any pixel where `(px & 0x00FFFFFF) == 0x00FF00FF` has its alpha forced to `0`.

This is applied when loading the cursor image for MenuScreen.

#### Cursor sizing

- Cursor assets may be any square dimension.
- Rendering is scaled to `theme.cursor_render_size` (a square) using nearest-neighbor sampling.
- Rendering uses alpha blending.


### 4.3 Theme.sfx

`sfx` is optional. Each field is optional.

- `on_move`: `.wav` filename under `Assets/Sounds/Menus/`
- `on_select`: `.wav` filename under `Assets/Sounds/Menus/`
- `on_back`: `.wav` filename under `Assets/Sounds/Menus/`

Validation:

- Must be strings if present.
- Must be safe filenames (no slashes) and end in `.wav`.
- If the file does not exist, it logs a warning and the field is ignored (set to null).


### 4.4 Views

`views` is an object mapping view-id → view-definition.

Example:

```json
"views": {
  "root": {
    "title": "MORTUM",
    "items": [
      { "label": "New Game", "action": { "kind": "command", "command": "load_timeline", "args": ["episode_1.json"] } },
      { "label": "Quit",     "action": { "kind": "command", "command": "quit", "args": [] } }
    ]
  },
  "options": {
    "title": "Options",
    "items": [
      { "label": "Back", "action": { "kind": "close" } }
    ]
  }
}
```

Validation:

- Each view must be a JSON object.
- `theme` inside a view is forbidden.
- `title` is optional (string).
- `items` is required (array).

Each `items[i]` must be an object with:

- `label` (required non-empty string)
- `action` (required object)


### 4.5 Actions

An item action is a JSON object with a required string field `kind`.

Supported kinds:

1) `"command"`

```json
{ "kind": "command", "command": "<console_command>", "args": ["...", "..."] }
```

- `command` is required (string).
- `args` is optional (array of strings). If omitted, arg_count = 0.

2) `"submenu"`

```json
{ "kind": "submenu", "view": "<view_id>" }
```

- `view` is required (string).
- Validation ensures the target view exists.

3) `"close"`

```json
{ "kind": "close" }
```

- Closes the current MenuScreen when activated.

4) `"none"`

```json
{ "kind": "none" }
```

- Equivalent “no-op” action type in data, but in the current MenuScreen implementation it closes the menu when activated.


## 5) Runtime Behavior

### 5.1 Input

MenuScreen uses these bindings:

- Up: `SDL_SCANCODE_UP`
- Down: `SDL_SCANCODE_DOWN`
- Select: `SDL_SCANCODE_RETURN`
- Escape/back: `SDL_SCANCODE_ESCAPE`

Behavior:

- Up/Down: changes selection (wrap-around).
- Enter:
  - On normal item: executes its action.
  - On submenu: pushes a view onto the menu stack.
  - On close/none: returns `SCREEN_RESULT_DONE`.
- Escape:
  - If in submenu depth > 1: goes “back” to previous view.
  - If at root:
    - **Timeline menus:** root ESC does nothing.
    - **Pause menus:** root ESC closes the menu.

Important pause-menu detail:

- When a pause menu is opened by pressing ESC, the menu ignores ESC until the key is released, preventing “open then immediately close in same frame”.

### 5.2 Menu stack and implicit Back

- Menu screens support a stack of views (`MENU_STACK_MAX` = 16).
- When `stack_depth > 1`, MenuScreen adds an implicit extra selectable item at the end named `Back`.

### 5.3 Rendering

- Background:
  - If `theme.background` loads, it is scaled to the full framebuffer using nearest-neighbor.
  - If not, the screen clears to black.

- Text:
  - Title (if present) is centered.
  - Items are rendered as centered text in a vertical column.

- Cursor:
  - Drawn to the left of the selected item.
  - Scaled to `theme.cursor_render_size` square.
  - Vertically centered within the item line.
  - If cursor is missing/unloaded, a `>` glyph is drawn instead.


## 6) Console Command Execution Semantics

Menu `command` actions ultimately run console commands.

Key runtime constraint:

- Commands are queued (deferred) via the `ConsoleCommandContext` so menu input does not re-enter the console/timeline/map teardown paths while the screen is still updating.

Behavior:

- Only one deferred line may be pending at a time.
- MenuScreen builds a command line by concatenating:
  - the command name
  - each arg as a double-quoted token
  - `\` and `"` are escaped
  - control chars are dropped

Execution:

- The main loop checks `console_ctx.deferred_line_pending` after the screen update and runs it via the normal console parser/dispatcher.

Implication:

- Menu command actions can safely trigger commands like `load_timeline ...`, `quit`, etc.
- There is no allowlist at the menu layer; any console command is callable.


## 7) Timeline Integration

Timeline event kind `menu` is supported.

### 7.1 Timeline JSON shape

A timeline event uses:

```json
{ "kind": "menu", "name": "main_menu.json", "on_complete": "loop" }
```

Fields:

- `kind`: must be `"scene"`, `"map"`, or `"menu"`.
- `name`:
  - for `menu`: must be a safe filename ending with `.json` under `Assets/Menus/`
- `on_complete`: `"advance" | "loop" | "load"`
- `target`: required only when `on_complete` is `"load"`

### 7.2 Runtime behavior

- TimelineFlow starts a MenuScreen for `menu` events using `invoked_from_timeline=true`.
- The timeline advances only when the active screen completes.

Patterns:

- Main-menu-as-timeline-event:
  - Use `on_complete: "loop"` to keep the event active.
  - Have menu items call `load_timeline "episode_1.json"` etc.


## 8) Pause Menu Integration

The engine opens `Assets/Menus/pause_menu.json` when:

- A map is loaded (`map_ok == true`),
- no other screen is active,
- the console is closed,
- and Escape is pressed (edge-detected).

Note: by default, Escape is also `input.bindings.release_mouse`. When mouse capture is currently engaged, pressing Escape first releases the mouse; pressing Escape again (with mouse released) will open the pause menu.

This creates a MenuScreen with:

- `invoked_from_timeline=false`

So:

- ESC at root closes the pause menu.
- Selecting a `close` action also closes it.


## 9) Validation and Tooling

`make validate` runs the asset validation tool which:

- loads the boot timeline,
- validates referenced scenes,
- validates referenced menus by calling `menu_load()`.

Because `menu_load()` performs eager existence checks for theme assets, validation is a strong signal the menu content is wired correctly.


## 10) Troubleshooting

Common problems and what they mean:

- “Menu background image not found …”
  - `theme.background` points to a missing file under `Assets/Images/Menus/Backgrounds/`.

- “Menu theme.cursor must be a safe .png filename …”
  - Cursor filenames must not contain slashes and must end in `.png`.

- Cursor draws as `>`
  - Cursor asset failed to load or was missing (cursor is optional).

- Menu doesn’t open on ESC
  - Console might be open, another screen might be active, or `map_ok` is false.


## 11) Reference: Effective Schema (LLM-friendly)

This is not a JSON-Schema file; it is a direct human-readable schema reflecting loader rules.

```jsonc
{
  "name": "string (non-empty)",
  "theme": {
    "background": "string (safe filename, *.png)",
    "cursor": "string (safe filename, *.png) (optional)",
    "cursor_render_size": "int 1..512 (optional; default 32)",
    "font": "string (safe filename, *.ttf)",
    "text_size": "int 1..128",
    "text_color": "string '#RRGGBB'",
    "music": "string (safe relpath, *.mid|*.midi) (optional)",
    "sfx": {
      "on_move": "string (safe filename, *.wav) (optional)",
      "on_select": "string (safe filename, *.wav) (optional)",
      "on_back": "string (safe filename, *.wav) (optional)"
    }
  },
  "views": {
    "<view_id>": {
      "title": "string (optional)",
      "items": [
        {
          "label": "string (non-empty)",
          "action": {
            "kind": "command|submenu|close|none",
            "command": "string (required if kind=command)",
            "args": ["string", "..."] ,
            "view": "string (required if kind=submenu)"
          }
        }
      ]
    }
  }
}
```
