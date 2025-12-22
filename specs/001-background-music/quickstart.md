# Quickstart: Background Music (MIDI via Fluidsynth)

## Overview
This feature adds level-specific background music using MIDI files played through Fluidsynth and sound fonts. Each map specifies its own MIDI file and sound font. If no sound font is specified, the default is used.

## MIDI & SoundFont Locations
- MIDI files: `Assets/Sounds/MIDI/`
- SoundFonts: `Assets/Sounds/SoundFonts/`
- Default SoundFont: `Assets/Sounds/SoundFonts/hl4mgm.sf2`

## Map Configuration
Each map JSON may include:
```json
{
  "bgmusic": "<MIDI filename>",
  "soundfont": "<SoundFont filename>"
}
```
If `soundfont` is omitted, the default is used.

## Dependency: Fluidsynth
- **Required for development and runtime**
- **Install on macOS:**
  ```sh
  brew install fluidsynth
  ```
- **Install on Linux (Debian/Ubuntu):**
  ```sh
  sudo apt-get install fluidsynth
  ```
- **Install on Windows:**
  Download from https://github.com/FluidSynth/fluidsynth/releases

- **Development:**
  You will need the Fluidsynth library and headers available for linking (e.g., `libfluidsynth` and `fluidsynth.h`).

## Usage Notes
- No SDL required for background music; only Fluidsynth is used.
- Music playback is managed per-level, with transitions and quitting handled as specified.
- If a level does not specify a MIDI file, no music will play.
- If a level does not specify a sound font, the default is used.

## Example
```json
{
  "bgmusic": "ACCRETION-DISK.MID",
  "soundfont": "hl4mgm.sf2"
}
```

## Troubleshooting
- If music does not play, ensure Fluidsynth is installed and the MIDI/SoundFont files exist.
- For missing/corrupt files, the game will play silence.

## References
- [FluidSynth Documentation](https://www.fluidsynth.org/)
- [FluidSynth GitHub](https://github.com/FluidSynth/fluidsynth)
