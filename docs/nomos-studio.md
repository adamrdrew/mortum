# Nomos Studio

Nomos Studio is a **standalone SDL2-based map editor** for the Mortum engine. It provides a visual interface for creating, editing, validating, and testing Mortum maps.

This document provides comprehensive developer-level documentation covering the architecture, code paths, APIs, and extension points.

---

## Table of Contents

1. [Overview](#overview)
2. [Building and Running](#building-and-running)
3. [Architecture](#architecture)
4. [Module Reference](#module-reference)
   - [nomos_main.c](#nomos_mainc---application-entry-point)
   - [nomos_document](#nomos_document---document-model)
   - [nomos_ui](#nomos_ui---user-interface)
   - [nomos_viewport](#nomos_viewport---map-viewport)
   - [nomos_save](#nomos_save---map-serialization)
   - [nomos_procgen](#nomos_procgen---procedural-generation)
   - [nomos_font](#nomos_font---text-rendering)
5. [Key Data Structures](#key-data-structures)
6. [Event Flow](#event-flow)
7. [Rendering Pipeline](#rendering-pipeline)
8. [Map Editing Operations](#map-editing-operations)
9. [Procedural Generation](#procedural-generation-details)
10. [Extension Guide](#extension-guide)
11. [Known Limitations](#known-limitations)

---

## Overview

Nomos Studio is designed to:

- **Load and display** existing Mortum maps from `Assets/Levels/`
- **Edit** map properties: sectors, walls, lights, entities, particles, and player start
- **Validate** maps against engine rules (via `map_validate()`)
- **Save** maps to JSON format
- **Generate** procedural maps using BSP-based room generation
- **Test** maps by launching Mortum with the `MAP` environment variable

The editor uses an **immediate-mode UI** paradigm for simple, stateless widget rendering.

---

## Building and Running

### Build Command

```bash
make nomos
```

This compiles all Nomos Studio source files and links them with required engine modules (asset loading, world management, validation, etc.).

### Run Command

```bash
make run-nomos
```

Or directly:

```bash
./build/nomos
```

### Dependencies

- **SDL2**: Window management, rendering, input handling
- **stb_truetype**: Font rendering (bundled in `third_party/stb/`)
- **Engine modules**: Reuses `map_loader`, `map_validate`, `world`, `entities`, etc.

### Source Files

Located in `tools/nomos_studio/`:

| File | Purpose |
|------|---------|
| `nomos_main.c` | Application entry point, main loop, initialization |
| `nomos.h` | Common types, enums, layout constants, colors |
| `nomos_document.c/h` | Document model (loaded map, selection, validation) |
| `nomos_ui.c/h` | UI framework (menus, panels, dialogs, widgets) |
| `nomos_viewport.c/h` | 2D map viewport (pan, zoom, selection, placement) |
| `nomos_save.c/h` | JSON serialization of maps |
| `nomos_procgen.c/h` | Procedural map generation |
| `nomos_font.c/h` | TTF font loading and text rendering |

---

## Architecture

### High-Level Structure

```
┌─────────────────────────────────────────────────────────────────┐
│                        NomosApp (g_app)                        │
├─────────────────────────────────────────────────────────────────┤
│  SDL_Window, SDL_Renderer                                       │
│  AssetPaths, EntityDefs                                         │
│  ├── NomosDocument  (currently loaded map + selection state)    │
│  ├── NomosUI        (menu state, palette mode, widget state)    │
│  ├── NomosViewport  (pan/zoom, dragging state)                  │
│  ├── NomosDialogState (active dialog: open/save/generate/error) │
│  └── NomosTextureList (texture thumbnails for browsing)         │
└─────────────────────────────────────────────────────────────────┘
```

### Main Loop

```c
while (running) {
    // 1. Process SDL events
    while (SDL_PollEvent(&event)) {
        nomos_handle_event(&event);  // UI -> Viewport -> Shortcuts
    }
    
    // 2. Update (handle dialog results, quit requests)
    nomos_update(dt);
    
    // 3. Render
    nomos_render();  // Viewport -> UI Panels -> Dialogs
}
```

### Module Relationships

```
nomos_main.c
    ├── nomos_document.c   (document operations)
    │       └── nomos_save.c   (save to JSON)
    │       └── nomos_procgen.c (generate maps)
    ├── nomos_ui.c         (UI rendering and events)
    │       └── nomos_font.c   (text rendering)
    └── nomos_viewport.c   (map rendering and interaction)
```

---

## Module Reference

### nomos_main.c - Application Entry Point

**Purpose**: Initializes SDL, loads assets, runs the main loop, handles cleanup.

#### Global State

```c
static NomosApp g_app;      // Application state
NomosFont g_nomos_font;     // Global font (extern)
```

#### Key Functions

| Function | Description |
|----------|-------------|
| `nomos_init()` | Initialize SDL, window, renderer, assets, font, document |
| `nomos_shutdown()` | Clean up all resources |
| `nomos_handle_event()` | Route events to UI, then viewport, then keyboard shortcuts |
| `nomos_update()` | Process dialog results, handle quit |
| `nomos_render()` | Render viewport, UI panels, dialogs |
| `nomos_do_menu_action()` | Execute menu commands (open, save, validate, etc.) |
| `find_assets_root()` | Locate the Assets directory relative to executable |

#### Initialization Sequence

1. `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)`
2. Create window with `SDL_WINDOW_ALLOW_HIGHDPI`
3. Create renderer with VSync
4. Calculate HiDPI scale factor
5. Find Assets directory (`find_assets_root`)
6. Initialize `AssetPaths` via `asset_paths_init()`
7. Load `EntityDefs` via `entity_defs_load()`
8. Initialize font via `nomos_font_init()`
9. Initialize document, UI, viewport, dialog

#### Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| Ctrl+O | Open map |
| Ctrl+S | Save |
| Ctrl+Shift+S | Save As |
| Ctrl+V | Validate |
| Ctrl+G | Generate |
| Ctrl+R | Run in Mortum |
| Delete/Backspace | Delete selected object |
| Escape | Deselect all |

---

### nomos_document - Document Model

**Purpose**: Manages the currently loaded map, selection state, and validation results.

#### NomosDocument Structure

```c
typedef struct NomosDocument {
    bool has_map;
    MapLoadResult map;           // The loaded map data (owned)
    char file_path[PATH_MAX];    // Relative to Assets/Levels/
    bool dirty;                  // Unsaved changes flag
    NomosSelection selection;    // Current selection
    NomosSelection hover;        // Hover state for UI feedback
    bool has_validation;
    MapValidationReport validation;
    int validation_scroll;
} NomosDocument;
```

#### Lifecycle Functions

| Function | Description |
|----------|-------------|
| `nomos_document_init()` | Zero-initialize document |
| `nomos_document_destroy()` | Free all resources |
| `nomos_document_clear()` | Clear map and validation, reset state |

#### I/O Operations

| Function | Description |
|----------|-------------|
| `nomos_document_load()` | Load map via `map_load()`, stores relative path |
| `nomos_document_save()` | Save map via `nomos_save_map()` |
| `nomos_document_validate()` | Run `map_validate()`, store report |
| `nomos_document_run_in_mortum()` | Save if dirty, launch mortum with `MAP` env var |

#### Selection Operations

| Function | Description |
|----------|-------------|
| `nomos_document_select()` | Set selection type and index |
| `nomos_document_deselect_all()` | Clear selection |
| `nomos_document_delete_selected()` | Delete currently selected entity/light/particle |

#### Object Manipulation

**Entities:**
- `nomos_document_add_entity(doc, def_name, x, y)` → int index
- `nomos_document_move_entity(doc, index, x, y)` → bool
- `nomos_document_remove_entity(doc, index)` → bool

**Lights:**
- `nomos_document_add_light(doc, x, y, z, radius, intensity)` → int index
- `nomos_document_move_light(doc, index, x, y)` → bool
- `nomos_document_remove_light(doc, index)` → bool
- `nomos_document_set_light_property(doc, index, property, value)` → bool

**Particles:**
- `nomos_document_add_particle(doc, x, y, z)` → int index
- `nomos_document_move_particle(doc, index, x, y)` → bool
- `nomos_document_remove_particle(doc, index)` → bool

**Player Start:**
- `nomos_document_move_player_start(doc, x, y)` → bool
- `nomos_document_set_player_start_angle(doc, angle_deg)` → bool

**Sector Properties:**
- `nomos_document_set_sector_floor_z(doc, index, value)` → bool
- `nomos_document_set_sector_ceil_z(doc, index, value)` → bool
- `nomos_document_set_sector_floor_tex(doc, index, tex)` → bool
- `nomos_document_set_sector_ceil_tex(doc, index, tex)` → bool
- `nomos_document_set_sector_light(doc, index, value)` → bool

**Wall Properties:**
- `nomos_document_set_wall_tex(doc, index, tex)` → bool
- `nomos_document_set_wall_end_level(doc, index, value)` → bool

#### Query Functions

| Function | Description |
|----------|-------------|
| `nomos_document_get_world_bounds()` | Get min/max XY of all vertices |
| `nomos_document_find_sector_at_point()` | Find sector containing point |

---

### nomos_ui - User Interface

**Purpose**: Immediate-mode UI framework with menus, panels, dialogs, and widgets.

#### Layout Structure

```c
typedef struct NomosLayout {
    SDL_Rect menu_bar;      // Top bar with File/Generate/Run menus
    SDL_Rect left_panel;    // Palette (entities, lights, particles, player start)
    SDL_Rect viewport;      // Main map view
    SDL_Rect right_panel;   // Inspector (properties of selected object)
    SDL_Rect status_bar;    // Bottom status bar
} NomosLayout;
```

Layout constants (in pixels at 1x scale):
- `NOMOS_MENU_HEIGHT`: 24
- `NOMOS_LEFT_PANEL_WIDTH`: 200
- `NOMOS_RIGHT_PANEL_WIDTH`: 280
- `NOMOS_STATUS_HEIGHT`: 22

#### NomosUI State

```c
typedef struct NomosUI {
    SDL_Renderer* renderer;
    int open_menu;              // -1 = none, 0 = File, 1 = Generate, 2 = Run
    bool menu_active;
    NomosPaletteMode palette_mode;
    int palette_scroll, palette_hovered, palette_selected;
    int inspector_scroll, validation_scroll;
    int mouse_x, mouse_y;
    bool mouse_down, mouse_clicked;
    bool any_widget_hovered;
} NomosUI;
```

#### Palette Modes

```c
typedef enum NomosPaletteMode {
    NOMOS_PALETTE_ENTITIES,      // Entity definitions from manifest
    NOMOS_PALETTE_LIGHTS,        // Point light placement
    NOMOS_PALETTE_PARTICLES,     // Particle emitter placement
    NOMOS_PALETTE_PLAYER_START,  // Player spawn point placement
} NomosPaletteMode;
```

#### Dialog System

```c
typedef enum NomosDialogType {
    NOMOS_DIALOG_NONE,
    NOMOS_DIALOG_OPEN,          // File open input
    NOMOS_DIALOG_SAVE_AS,       // File save input
    NOMOS_DIALOG_GENERATE,      // Procgen parameters
    NOMOS_DIALOG_ERROR,         // Error message display
    NOMOS_DIALOG_TEXTURE_PICKER,// Texture selection grid
} NomosDialogType;
```

Dialog lifecycle:
1. `nomos_dialog_show_*()` - Open a dialog
2. `nomos_dialog_handle_event()` - Process input
3. `nomos_dialog_render()` - Draw the dialog
4. `nomos_dialog_poll_result()` - Check for completion

#### Immediate-Mode Widgets

All widgets return `NomosWidgetResult`:

```c
typedef struct NomosWidgetResult {
    bool hovered;
    bool clicked;
    bool value_changed;
} NomosWidgetResult;
```

Available widgets:
- `nomos_ui_button(ui, renderer, rect, label)` - Clickable button
- `nomos_ui_label(ui, renderer, rect, text, dim)` - Text label
- `nomos_ui_checkbox(ui, renderer, rect, label, &value)` - Toggle checkbox
- `nomos_ui_slider_int(ui, renderer, rect, label, &value, min, max)` - Integer slider
- `nomos_ui_slider_float(ui, renderer, rect, label, &value, min, max)` - Float slider
- `nomos_ui_text_input(ui, renderer, rect, buffer, size, &cursor)` - Text field

#### Rendering Flow

The UI renders in this order:
1. Left panel (palette tabs + items)
2. Right panel (inspector)
3. Status bar
4. Menu bar background
5. Menu bar text
6. **Menu dropdowns** (rendered last for correct z-order)

---

### nomos_viewport - Map Viewport

**Purpose**: 2D top-down map view with pan/zoom and object selection/placement.

#### NomosViewport State

```c
typedef struct NomosViewport {
    float pan_x, pan_y;         // World coordinates at viewport center
    float zoom;                 // Pixels per world unit
    bool panning;               // Middle-click or Shift+Left panning
    int pan_start_mouse_x, pan_start_mouse_y;
    float pan_start_world_x, pan_start_world_y;
    bool dragging;              // Dragging a selected object
    NomosSelectionType drag_type;
    int drag_index;
    float drag_offset_x, drag_offset_y;
} NomosViewport;
```

#### Coordinate Transforms

```c
// World (map units) → Screen (pixels)
void nomos_viewport_world_to_screen(vp, rect, wx, wy, &sx, &sy);

// Screen (pixels) → World (map units)
void nomos_viewport_screen_to_world(vp, rect, sx, sy, &wx, &wy);
```

Coordinate system:
- **World**: +X right, +Y up (matches game)
- **Screen**: +X right, +Y down (SDL convention)
- Viewport center corresponds to `(pan_x, pan_y)` in world space

#### Interaction Modes

**Zooming** (Mouse wheel in viewport):
- Zoom factor: 1.15x per scroll tick
- Zoom range: 2.0 to 200.0 pixels per unit
- Zoom toward mouse cursor position

**Panning** (Middle-click or Shift+Left drag):
- Stores start position and updates pan during drag

**Selection** (Left-click):
Priority order for hit testing:
1. Entities (0.5 unit hit radius)
2. Lights (0.3 unit hit radius)
3. Particles (0.3 unit hit radius)
4. Player start (0.4 unit hit radius)
5. Walls (8 pixel threshold, distance to segment)
6. Sectors (point-in-polygon test)

**Placement** (Left-click on empty space):
If nothing is selected and the click is in a valid sector:
- Places object based on current palette mode
- Entity: uses `palette_selected` to get def name from `EntityDefs`
- Light: creates with default radius=4, intensity=1
- Particle: creates with default emitter parameters
- Player Start: moves the player start position

**Dragging** (Left-click-and-drag on object):
- Stores offset from click point to object center
- Calls appropriate `nomos_document_move_*()` during motion

#### Rendering

Draws in this order:
1. Background grid (optional, based on zoom level)
2. Sectors as filled polygons (color-coded by selection)
3. Walls as lines (solid=opaque, portal=semi-transparent)
4. Lights as yellow circles
5. Particles as magenta circles
6. Entities as cyan circles
7. Player start as green arrow
8. Selection highlight

---

### nomos_save - Map Serialization

**Purpose**: Serialize `MapLoadResult` to Mortum-compatible JSON.

#### API

```c
bool nomos_save_map(const MapLoadResult* map, const char* filepath);
```

#### JSON Structure

```json
{
  "version": 1,
  "bgmusic": "optional.mid",
  "soundfont": "optional.sf2",
  "sky": "optional.png",
  "player_start": {"x": 5.0, "y": 5.0, "angle_deg": 0.0},
  "vertices": [
    {"x": 0.0, "y": 0.0},
    ...
  ],
  "sectors": [
    {"id": 0, "floor_z": 0.0, "ceil_z": 4.0, "floor_tex": "...", "ceil_tex": "...", "light": 1.0},
    ...
  ],
  "walls": [
    {"v0": 0, "v1": 1, "front_sector": 0, "back_sector": -1, "tex": "..."},
    ...
  ],
  "lights": [
    {"x": 5.0, "y": 5.0, "z": 3.0, "radius": 4.0, "intensity": 1.0},
    ...
  ],
  "sounds": [...],
  "particles": [...],
  "entities": [...],
  "doors": [...]
}
```

#### Implementation Notes

- Uses `fprintf` for JSON generation (no external library)
- Escapes special characters in strings
- Uses `floor_z_origin` for authored floor height (not runtime `floor_z`)
- Uses `base_tex` for wall texture (not runtime `tex`)
- Only writes optional fields if they have non-default values

---

### nomos_procgen - Procedural Generation

**Purpose**: Generate valid Mortum maps using a hub-and-spoke room layout.

#### NomosProcGenParams

```c
typedef struct NomosProcGenParams {
    float min_x, min_y, max_x, max_y;  // Map bounds
    int target_room_count;              // Approximate room count
    float min_room_size, max_room_size; // Room dimensions
    float min_floor_z, max_floor_z;     // Floor height range
    float min_ceil_height, max_ceil_height; // Ceiling height above floor
    uint32_t seed;                      // Random seed (0 = time-based)
    char floor_tex[64], ceil_tex[64], wall_tex[64];
} NomosProcGenParams;
```

#### API

```c
void nomos_procgen_params_default(NomosProcGenParams* params);
bool nomos_procgen_generate(MapLoadResult* out, const NomosProcGenParams* params);
```

#### Generation Algorithm

The generator uses a **hub-and-spoke** pattern for guaranteed connectivity:

1. **Create central hub room** at map center
2. **Create peripheral rooms** arranged in a circle around the hub
3. **Create corridors** connecting each peripheral room directly to the hub
4. **Build portal walls** with proper bidirectional connections

**Critical for validity:**
- Each sector has a closed loop of front-side walls (4 walls for rectangles)
- Portal walls are created in pairs: (A→B from corridor, B→A from room)
- Hub sector is sector 0, player starts in hub → all sectors reachable

#### Corridor Creation

For each corridor connecting room R to hub H:
1. Calculate direction vector from hub center to room center
2. Find exit point on hub boundary and entry point on room boundary
3. Create 4 corridor vertices forming a rectangle
4. Create corridor sector with 4 walls
5. Create bidirectional portal walls:
   - Wall from corridor to hub (`front=corridor, back=hub`)
   - Wall from hub to corridor (`front=hub, back=corridor`)
   - Wall from corridor to room (`front=corridor, back=room`)
   - Wall from room to corridor (`front=room, back=corridor`)
6. Create solid walls for corridor sides

---

### nomos_font - Text Rendering

**Purpose**: Load TTF fonts via stb_truetype and render text to SDL textures.

#### NomosFont Structure

```c
typedef struct NomosFont {
    SDL_Texture* atlas;              // Texture atlas with all glyphs
    int atlas_w, atlas_h;
    int line_height, ascent;
    NomosGlyph glyphs[256];          // Glyph metrics for ASCII
    float ui_scale;                  // HiDPI scale factor
} NomosFont;
```

#### API

```c
bool nomos_font_init(NomosFont* font, SDL_Renderer* renderer, 
    const AssetPaths* paths, const char* ttf_filename, 
    int pixel_height, float ui_scale);

void nomos_font_destroy(NomosFont* font);

void nomos_font_draw(NomosFont* font, SDL_Renderer* renderer, 
    int x, int y, const char* text, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

int nomos_font_measure_width(NomosFont* font, const char* text);
int nomos_font_line_height(NomosFont* font);
```

#### Implementation Notes

- Uses ProggyClean.ttf from `Assets/Fonts/`
- Creates texture atlas at initialization
- Pixel format: `SDL_PIXELFORMAT_ARGB8888` for proper alpha blending
- Scales font size by `ui_scale` for HiDPI displays
- Global font instance: `g_nomos_font`

---

## Key Data Structures

### Selection Types

```c
typedef enum NomosSelectionType {
    NOMOS_SEL_NONE = 0,
    NOMOS_SEL_SECTOR,
    NOMOS_SEL_WALL,
    NOMOS_SEL_ENTITY,
    NOMOS_SEL_LIGHT,
    NOMOS_SEL_PARTICLE,
    NOMOS_SEL_PLAYER_START,
} NomosSelectionType;

typedef struct NomosSelection {
    NomosSelectionType type;
    int index;
} NomosSelection;
```

### Menu Actions

```c
typedef enum NomosMenuAction {
    NOMOS_MENU_NONE = 0,
    NOMOS_MENU_OPEN,
    NOMOS_MENU_SAVE,
    NOMOS_MENU_SAVE_AS,
    NOMOS_MENU_VALIDATE,
    NOMOS_MENU_EXIT,
    NOMOS_MENU_GENERATE,
    NOMOS_MENU_RUN,
} NomosMenuAction;
```

### Engine Types Used

From `map_loader.h`:
- `MapLoadResult` - Container for loaded map data
- `MapEntityPlacement` - Entity spawn point
- `MapParticleEmitter` - Particle emitter definition
- `MapSoundEmitter` - Sound emitter definition
- `MapDoor` - Door definition

From `world.h`:
- `World` - Geometry container (vertices, sectors, walls, lights)
- `Vertex` - 2D point
- `Sector` - Room with floor/ceiling
- `Wall` - Edge between vertices
- `PointLight` - Dynamic point light

From `entities.h`:
- `EntityDefs` - Loaded entity definitions from manifest
- `EntityDef` - Single entity type definition

---

## Event Flow

```
SDL_Event
    │
    ▼
nomos_handle_event()
    │
    ├─► nomos_ui_handle_event()
    │       ├─► nomos_dialog_handle_event() (if dialog active)
    │       ├─► Menu interaction
    │       └─► Panel interaction
    │       (returns true if consumed)
    │
    ├─► SDL_QUIT → set request_quit
    │
    ├─► SDL_WINDOWEVENT → update window size
    │
    ├─► SDL_KEYDOWN → keyboard shortcuts
    │
    └─► SDL_MOUSE* → nomos_viewport_handle_event()
            ├─► Zoom (wheel)
            ├─► Pan (middle/shift+left drag)
            ├─► Select (left click)
            ├─► Place (left click in empty space)
            └─► Drag (left click-and-drag on object)
```

---

## Rendering Pipeline

```
nomos_render()
    │
    ├─► SDL_RenderClear() (dark background)
    │
    ├─► nomos_ui_calculate_layout() (compute panel rects)
    │
    ├─► nomos_viewport_render()
    │       ├─► Grid lines
    │       ├─► Sector fills
    │       ├─► Wall lines
    │       ├─► Light icons
    │       ├─► Particle icons
    │       ├─► Entity icons
    │       ├─► Player start icon
    │       └─► Selection highlight
    │
    ├─► nomos_ui_render()
    │       ├─► Left panel (palette)
    │       │       ├─► Tab buttons (Entities/Lights/Particles/Player)
    │       │       └─► Item list with scroll
    │       ├─► Right panel (inspector)
    │       │       └─► Properties based on selection type
    │       ├─► Status bar
    │       ├─► Menu bar background + text
    │       └─► Menu dropdowns (z-ordered on top)
    │
    ├─► nomos_dialog_render() (modal dialog if active)
    │
    └─► SDL_RenderPresent()
```

---

## Map Editing Operations

### Loading a Map

```
User: Ctrl+O or File → Open
    │
    ▼
nomos_do_menu_action(NOMOS_MENU_OPEN)
    │
    ▼
nomos_dialog_show_open(&dialog, &paths)
    │
    ▼
User enters filename, clicks OK
    │
    ▼
nomos_dialog_poll_result() returns true
    │
    ▼
nomos_document_load(&doc, &paths, filename)
    │
    ├─► map_load(&map, paths, filename)
    ├─► Clear old document
    ├─► Store map and path
    └─► Set dirty = false
    │
    ▼
nomos_viewport_fit_to_map(&viewport, &doc)
```

### Saving a Map

```
User: Ctrl+S or File → Save
    │
    ▼
nomos_do_menu_action(NOMOS_MENU_SAVE)
    │
    ├─► If no file_path: show Save As dialog
    │
    └─► nomos_document_save(&doc, &paths)
            │
            ├─► asset_path_join(paths, "Levels", file_path)
            ├─► nomos_save_map(&map, full_path)
            └─► Set dirty = false
```

### Validating a Map

```
User: Ctrl+V or File → Validate
    │
    ▼
nomos_do_menu_action(NOMOS_MENU_VALIDATE)
    │
    ▼
nomos_document_validate(&doc, &paths)
    │
    ├─► Clear old validation report
    ├─► map_validation_report_init(&validation)
    ├─► map_validate_set_report_sink(&validation)
    ├─► map_validate(&world, player_x, player_y, doors, door_count)
    ├─► map_validate_set_report_sink(NULL)
    └─► Store results in doc.validation
```

### Running in Mortum

```
User: Ctrl+R or Run → Test in Mortum
    │
    ▼
nomos_do_menu_action(NOMOS_MENU_RUN)
    │
    ▼
nomos_document_run_in_mortum(&doc, &paths)
    │
    ├─► If dirty: nomos_document_save()
    ├─► Find mortum executable
    ├─► setenv("MAP", file_path, 1)
    └─► posix_spawn(mortum)
```

---

## Procedural Generation Details

### Validity Requirements

The Mortum engine enforces strict map validity rules (see `docs/map.md`):

1. **Closed boundary loops**: Each sector must have a closed polygon of front-side walls
2. **Bidirectional portals**: Portal walls require two directed walls (A→B and B→A)
3. **Contiguity**: All sectors must be reachable from player start via portals
4. **Player in sector**: Player start must be inside a sector

### Hub-and-Spoke Strategy

The generator guarantees validity by construction:

```
                    ┌─────────┐
                    │ Room 2  │
                    └────┬────┘
                         │ corridor
    ┌─────────┐     ┌────┴────┐     ┌─────────┐
    │ Room 1  ├─────┤   HUB   ├─────┤ Room 3  │
    └─────────┘     └────┬────┘     └─────────┘
                         │
                    ┌────┴────┐
                    │ Room 4  │
                    └─────────┘
```

- Player starts in hub (sector 0)
- Every room connects to hub via corridor
- Hub is reachable from all rooms → all sectors connected

### Portal Wall Creation

For a portal between sectors A and B sharing edge (v0, v1):

```c
// Wall from A to B
Wall w1 = { .v0 = v0, .v1 = v1, .front_sector = A, .back_sector = B };

// Wall from B to A (reversed direction)
Wall w2 = { .v0 = v1, .v1 = v0, .front_sector = B, .back_sector = A };
```

Both walls use the same vertices but in opposite order. This ensures:
- Sector A's boundary includes edge v0→v1
- Sector B's boundary includes edge v1→v0
- Both sectors form valid closed loops

---

## Extension Guide

### Adding a New Object Type

1. **Add selection type** in `nomos.h`:
   ```c
   typedef enum NomosSelectionType {
       // ...
       NOMOS_SEL_NEW_TYPE,
   } NomosSelectionType;
   ```

2. **Add document operations** in `nomos_document.c/h`:
   ```c
   int nomos_document_add_newtype(NomosDocument* doc, ...);
   bool nomos_document_move_newtype(NomosDocument* doc, int index, float x, float y);
   bool nomos_document_remove_newtype(NomosDocument* doc, int index);
   ```

3. **Add viewport hit testing** in `nomos_viewport.c`:
   - Add loop in `nomos_viewport_handle_event()` for click detection
   - Add dragging case in motion handler

4. **Add viewport rendering** in `nomos_viewport.c`:
   - Add drawing code in `nomos_viewport_render()`

5. **Add inspector UI** in `nomos_ui.c`:
   - Add case in inspector rendering for `NOMOS_SEL_NEW_TYPE`
   - Add property sliders/inputs

6. **Add palette mode** (optional) in `nomos.h` and `nomos_ui.c`

7. **Add serialization** in `nomos_save.c`:
   - Add JSON array writing for new type

### Adding a New Menu Item

1. **Add action enum** in `nomos.h`:
   ```c
   typedef enum NomosMenuAction {
       // ...
       NOMOS_MENU_NEW_ACTION,
   } NomosMenuAction;
   ```

2. **Add menu entry** in `nomos_ui.c` `render_menu_bar()`:
   - Add button to appropriate dropdown

3. **Handle action** in `nomos_main.c` `nomos_do_menu_action()`

4. **Add keyboard shortcut** (optional) in `nomos_handle_event()`

### Adding a New Dialog

1. **Add dialog type** in `nomos_ui.h`:
   ```c
   typedef enum NomosDialogType {
       // ...
       NOMOS_DIALOG_NEW,
   } NomosDialogType;
   ```

2. **Add dialog fields** to `NomosDialogState` if needed

3. **Add show function**:
   ```c
   void nomos_dialog_show_new(NomosDialogState* dialog) {
       dialog->type = NOMOS_DIALOG_NEW;
       dialog->has_result = false;
       // initialize fields
   }
   ```

4. **Add rendering** in `nomos_dialog_render()`

5. **Add event handling** in `nomos_dialog_handle_event()`

6. **Add result handling** in `nomos_update()` after `nomos_dialog_poll_result()`

---

## Known Limitations

### Current Limitations

1. **No undo/redo**: Changes cannot be undone
2. **No vertex editing**: Cannot add/move/delete vertices directly
3. **No wall editing**: Cannot create new walls or change wall topology
4. **No sector creation**: Cannot create new sectors manually
5. **No texture browser**: Texture list is a stub (not loaded)
6. **No copy/paste**: Cannot duplicate objects
7. **No grid snapping**: Objects are placed at mouse position exactly
8. **No multi-selection**: Can only select one object at a time
9. **No door editing**: Doors are serialized but not editable in UI
10. **macOS only**: Uses `posix_spawn()` for running Mortum

### Future Improvements

- Implement undo/redo stack
- Add geometry editing tools (split wall, extrude sector, etc.)
- Load texture thumbnails for browser
- Add grid snapping with configurable grid size
- Add multi-select with Shift+click
- Add door creation/editing UI
- Cross-platform process spawning
- Autosave and recovery
