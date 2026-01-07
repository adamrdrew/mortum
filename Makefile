# Fluidsynth for MIDI playback
FLUIDSYNTH_CFLAGS := $(shell pkg-config --cflags fluidsynth)
FLUIDSYNTH_LIBS := $(shell pkg-config --libs fluidsynth)

CC ?= clang

# Prefer sdl2-config when available.
# Prefer sdl2-config when available; fall back to pkg-config.
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS   := $(shell sdl2-config --libs 2>/dev/null)
ifeq ($(strip $(SDL_CFLAGS)),)
  SDL_CFLAGS := $(shell pkg-config sdl2 --cflags 2>/dev/null)
  SDL_LIBS := $(shell pkg-config sdl2 --libs 2>/dev/null)
endif

CSTD := -std=c11
WARN := -Wall -Wextra -Wpedantic -Werror
DBG  := -O0 -g
REL  := -O2

CPPFLAGS := -Iinclude -Ithird_party
CFLAGS_COMMON := $(CSTD) $(WARN) $(SDL_CFLAGS) $(FLUIDSYNTH_CFLAGS)

BIN_DIR := build
BIN := $(BIN_DIR)/mortum

# Optional: run a specific map via `make run MAP=arena.json`
RUN_MAP ?=

# Optional: pass extra args to the binary via `make run RUN_ARGS="--flag"`
RUN_ARGS ?=

# Back-compat alias (some docs/specs use MAP=...)
MAP ?=
ifneq ($(strip $(MAP)),)
  RUN_MAP := $(MAP)
endif

SRC := \
  src/main.c \
  src/core/log.c \
  src/core/crash_diag.c \
  src/core/config.c \
  src/core/path_safety.c \
  src/core/game_loop.c \
  src/platform/platform.c \
  src/platform/window_sdl.c \
  src/platform/input_sdl.c \
  src/platform/time_sdl.c \
  src/platform/fs_sdl.c \
  src/platform/audio_sdl.c \
  src/render/framebuffer.c \
  src/render/present_sdl.c \
  src/render/draw.c \
  src/render/camera.c \
  src/render/raycast.c \
  src/render/texture.c \
  src/render/level_mesh.c \
  src/render/lighting.c \
  src/render/vga_palette.c \
  src/assets/json.c \
  src/assets/asset_paths.c \
  src/assets/timeline_loader.c \
  src/assets/menu_loader.c \
  src/assets/hud_loader.c \
  src/assets/map_loader.c \
  src/assets/map_validate.c \
  src/assets/image_bmp.c \
	src/assets/image_png.c \
  src/assets/sound_wav.c \
  src/assets/scene_loader.c \
  src/assets/midi_player.c \
  src/game/world.c \
  src/game/physics_body.c \
  src/game/perf_trace.c \
  src/game/player.c \
  src/game/game_state.c \
  src/game/hud.c \
  src/game/font.c \
  src/game/weapons.c \
  src/game/ammo.c \
  src/game/weapon_defs.c \
  src/game/weapon_visuals.c \
  src/game/weapon_view.c \
  src/game/postfx.c \
  src/game/upgrades.c \
  src/game/mortum.c \
  src/game/inventory.c \
  src/game/notifications.c \
  src/game/purge_item.c \
  src/game/rules.c \
  src/game/tuning.c \
  src/game/collision.c \
  src/game/sector_height.c \
  src/game/doors.c \
  src/game/player_controller.c \
  src/game/sound_emitters.c \
  src/game/particle_emitters.c \
  src/game/gore.c \
  src/game/particles.c \
  src/game/console.c \
  src/game/console_commands.c \
  src/game/timeline_flow.c \
  src/game/level_start.c \
  src/game/map_music.c \
  src/game/screen_runtime.c \
  src/game/scene_screen.c \
  src/game/menu_screen.c

SRC := $(SRC) \
	src/game/entities.c

SRC := $(SRC) \
  src/game/debug_dump.c

SRC := $(SRC) \
  src/game/debug_overlay.c

THIRD_SRC := \
  third_party/lodepng.c

OBJ := $(SRC:src/%.c=$(BIN_DIR)/obj/%.o) $(THIRD_SRC:third_party/%.c=$(BIN_DIR)/obj/third_party/%.o)

LIB_SRC := $(filter-out src/main.c,$(SRC))
LIB_OBJ := $(LIB_SRC:src/%.c=$(BIN_DIR)/obj/%.o) $(THIRD_SRC:third_party/%.c=$(BIN_DIR)/obj/third_party/%.o)

TOOL_VALIDATE := $(BIN_DIR)/validate_assets
TOOL_VALIDATE_OBJ := $(BIN_DIR)/obj/tools/validate_assets.o

.PHONY: all release run test validate clean

all: CFLAGS := $(CFLAGS_COMMON) $(DBG)
all: $(BIN)

release: CFLAGS := $(CFLAGS_COMMON) $(REL)
release: $(BIN)

run: CFLAGS := $(CFLAGS_COMMON) $(DBG)
run: $(BIN) ; $(BIN) $(RUN_MAP) $(RUN_ARGS)

test: ; @echo "No tests wired yet." ; exit 0

validate: CFLAGS := $(CFLAGS_COMMON) $(DBG)
validate: $(TOOL_VALIDATE) ; $(TOOL_VALIDATE) $(RUN_MAP)

clean: ; @rm -rf $(BIN_DIR)

$(BIN): $(OBJ) ; @mkdir -p $(BIN_DIR) ; $(CC) $(OBJ) -o $@ $(SDL_LIBS) $(FLUIDSYNTH_LIBS)

$(BIN_DIR)/obj/%.o: src/%.c ; @mkdir -p $(dir $@) ; $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BIN_DIR)/obj/third_party/%.o: third_party/%.c ; @mkdir -p $(dir $@) ; $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TOOL_VALIDATE_OBJ): tools/validate_assets.c ; @mkdir -p $(dir $@) ; $(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(TOOL_VALIDATE): $(LIB_OBJ) $(TOOL_VALIDATE_OBJ) ; @mkdir -p $(BIN_DIR) ; $(CC) $(LIB_OBJ) $(TOOL_VALIDATE_OBJ) -o $@ $(SDL_LIBS) $(FLUIDSYNTH_LIBS)
