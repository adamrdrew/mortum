# Contributing

## Build philosophy

- Keep builds boring: `make`, `make run`, `make test`, `make clean`.
- Keep dependencies minimal; prefer vendoring tiny libs over complex package managers.
- Keep code readable; avoid clever tricks.

## Code style

- C11.
- Warnings treated seriously.
- Prefer small functions with honest names.
- Prefer explicit init/destroy and one `cleanup:` block for error paths.

## Platform rules

- SDL2 usage is confined to `src/platform/` and `src/render/present_sdl.c`.
- No platform-specific `#ifdef` in gameplay.
