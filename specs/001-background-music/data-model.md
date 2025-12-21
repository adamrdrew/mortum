# Data Model: Background Music

## Entities

### Level
- `name`: string
- `bgmusic`: string (MIDI filename, optional)
- `soundfont`: string (SoundFont filename, optional; defaults to hl4mgm.sf2)

### Music Track
- `filename`: string (MIDI file)
- `soundfont`: string (SoundFont file)
- `loop`: bool (always true for bgmusic)

## Relationships
- Each Level may specify a Music Track (MIDI + SoundFont)
- If not specified, no music (silence)
- If SoundFont not specified, use default

## Validation Rules
- MIDI file must exist in Assets/Sounds/MIDI/
- SoundFont must exist in Assets/Sounds/SoundFonts/
- If missing/corrupt, play silence

## State Transitions
- On level load: start music (if specified)
- On level change: stop old music, start new
- On quit: stop music
