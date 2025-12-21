# Fluidsynth MIDI Playback Integration: Best Practices (C99, Cross-Platform)

## Overview
This document outlines best practices for integrating Fluidsynth-based MIDI playback in a C99 cross-platform game engine, without SDL. It covers installation, linking, API usage, error handling, resource management, and rationale for choosing Fluidsynth over alternatives.

---

## 1. Installing Fluidsynth for Development

### macOS
- Use Homebrew:
  ```sh
  brew install fluidsynth
  ```
  - Installs library and CLI tools.
  - Headers: `/usr/local/include/fluidsynth.h` or `/opt/homebrew/include/fluidsynth.h`
  - Library: `/usr/local/lib/libfluidsynth.dylib` or `/opt/homebrew/lib/libfluidsynth.dylib`

### Linux (Debian/Ubuntu)
- Use apt:
  ```sh
  sudo apt-get install libfluidsynth-dev fluidsynth
  ```

### Windows
- Use vcpkg or download prebuilt binaries:
  ```sh
  vcpkg install fluidsynth
  ```
- Or build from source: https://github.com/FluidSynth/fluidsynth

---

## 2. Linking Fluidsynth in a Makefile

Add to your Makefile:

```makefile
FLUIDSYNTH_CFLAGS := $(shell pkg-config --cflags fluidsynth)
FLUIDSYNTH_LIBS := $(shell pkg-config --libs fluidsynth)

# Example
CFLAGS += $(FLUIDSYNTH_CFLAGS)
LDFLAGS += $(FLUIDSYNTH_LIBS)
```

If `pkg-config` is unavailable, manually specify:
```makefile
CFLAGS += -I/path/to/fluidsynth/include
LDFLAGS += -L/path/to/fluidsynth/lib -lfluidsynth
```

---

## 3. Fluidsynth API Usage: Load, Play, Stop MIDI

### Initialization
```c
#include <fluidsynth.h>

fluid_settings_t *settings = new_fluid_settings();
fluid_synth_t *synth = new_fluid_synth(settings);
fluid_audio_driver_t *adriver = new_fluid_audio_driver(settings, synth);
```

### Load SoundFont
```c
int sf_id = fluid_synth_sfload(synth, "path/to/soundfont.sf2", 1);
if (sf_id == FLUID_FAILED) { /* handle error */ }
```

### Load and Play MIDI File
```c
fluid_player_t *player = new_fluid_player(synth);
if (fluid_player_add(player, "path/to/file.mid") != FLUID_OK) { /* handle error */ }
if (fluid_player_play(player) != FLUID_OK) { /* handle error */ }
```

### Stop Playback
```c
fluid_player_stop(player);
```

### Resource Cleanup
```c
delete_fluid_player(player);
delete_fluid_audio_driver(adriver);
delete_fluid_synth(synth);
delete_fluid_settings(settings);
```

---

## 4. Error Handling
- Always check return values (`FLUID_OK`, `FLUID_FAILED`).
- Use `fluid_synth_error()` for error messages if available.
- Log errors and abort/skip gracefully.

---

## 5. Clean Resource Management
- Ensure all objects created with `new_*` are deleted with corresponding `delete_*`.
- Stop playback before deleting player.
- Unload soundfonts if needed with `fluid_synth_sfunload()`.
- Avoid memory leaks by cleaning up on game exit or asset reload.

---

## 6. Alternatives Considered
- **Timidity++**: CLI only, no C API, less flexible.
- **WildMIDI**: Simple, but limited features and soundfont support.
- **BASS/MODPLUG**: Proprietary or limited MIDI support.
- **Custom MIDI Synth**: High complexity, poor cross-platform support.

### Rationale for Fluidsynth
- Mature, open-source, actively maintained.
- Full-featured C API.
- Supports SoundFonts (SF2), flexible audio output.
- Cross-platform (Windows, macOS, Linux).
- Good documentation and community support.

---

## 7. Technical Context: Clarifications
- **No SDL**: Directly use Fluidsynth's audio driver or integrate with platform-specific audio APIs if needed.
- **SoundFont Selection**: Load at runtime, allow user/asset selection.
- **MIDI Control**: Use `fluid_player_*` API for playback control.
- **Error Handling**: Always check API return codes, log errors.
- **Resource Management**: Delete all objects, stop playback before cleanup.

---

## References
- [Fluidsynth API Docs](https://www.fluidsynth.org/api/)
- [Fluidsynth GitHub](https://github.com/FluidSynth/fluidsynth)
- [SoundFont Resources](https://sites.google.com/site/soundfonts4u/)

---

## Example Minimal Integration (C99)
```c
#include <fluidsynth.h>

void play_midi(const char *sf2, const char *mid) {
    fluid_settings_t *settings = new_fluid_settings();
    fluid_synth_t *synth = new_fluid_synth(settings);
    fluid_audio_driver_t *adriver = new_fluid_audio_driver(settings, synth);
    int sf_id = fluid_synth_sfload(synth, sf2, 1);
    if (sf_id == FLUID_FAILED) { /* handle error */ }
    fluid_player_t *player = new_fluid_player(synth);
    if (fluid_player_add(player, mid) != FLUID_OK) { /* handle error */ }
    fluid_player_play(player);
    // ...wait or poll for completion...
    fluid_player_stop(player);
    delete_fluid_player(player);
    delete_fluid_audio_driver(adriver);
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
}
```

---

## Summary
Fluidsynth is the recommended solution for MIDI playback in C99 cross-platform games due to its robust API, soundfont support, and active development. Follow best practices for installation, linking, error handling, and resource management to ensure reliable integration.
