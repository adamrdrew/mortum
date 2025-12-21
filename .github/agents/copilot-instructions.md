# mortum Development Guidelines

Auto-generated from all feature plans. Last updated: 2025-12-19

## Active Technologies
- C11 (clang primary, gcc secondary) + SDL2 (window/input/audio) (001-mortum-vision-spec)
- JSON files in `Assets/` (maps, episodes) (001-mortum-vision-spec)
- C11 + SDL2 (window/input/audio), vendored `jsmn` via `assets/json.c` (002-texture-floors-ceilings)
- JSON asset files under `Assets/` (maps, episodes, images, sounds) (002-texture-floors-ceilings)
- JSON files in `Assets/` (maps, episodes); PNG assets in `Assets/Images/` (003-weapon-system)
- C99 (cross-platform) + Fluidsynth (libfluidsynth, fluidsynth.h) (001-background-music)
- JSON map files (Assets/Levels/*.json), MIDI files, SoundFonts (001-background-music)

- C11 (Clang primary; GCC supported) + SDL2 initially (SDL3 is a later migration option); optional small, vendored parsers/decoders only if they keep builds boring (001-mortum-vision-spec)

## Project Structure

```text
src/
tests/
```

## Commands

# Add commands for C11 (Clang primary; GCC supported)

## Code Style

C11 (Clang primary; GCC supported): Follow standard conventions

## Recent Changes
- 001-background-music: Added C99 (cross-platform) + Fluidsynth (libfluidsynth, fluidsynth.h)
- 003-weapon-system: Added C11 (clang primary, gcc secondary) + SDL2 (window/input/audio)
- 003-weapon-system: Added SDL2 (window/input/audio)


<!-- MANUAL ADDITIONS START -->
<!-- MANUAL ADDITIONS END -->
