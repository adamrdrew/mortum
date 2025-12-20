# Quickstart — Weapon Viewmodels, Icons, and Switching

## Build

- `make clean && make`
- Optional: `make test`

## Run

- `make run RUN_MAP=arena.json`

## Controls (after implementation)

- Move: WASD
- Look: mouse (relative)
- Fire: left mouse
- Dash: Shift
- Weapon select: 1–5
- Weapon cycle: Q (prev), E (next)
- Use purge item: F (moved from E)

## Visual Sanity Checks (after implementation)

- Viewmodel: weapon sprite is centered near bottom, and HUD hides the bottom few pixels.
- Sway: while walking, weapon gently bobs/sways; when stopping, sway settles.
- Shoot anim: on firing a shot, SHOOT-1..6 plays and returns to IDLE.
- HUD icon: equipped weapon icon is visible and changes on weapon switch.
- Pickups: weapon pickup entities render using `WEAPON-PICKUP.png` (not the old sprites atlas).

## Troubleshooting

- If a weapon sprite/icon is missing, you should see a single error log for that texture, but gameplay should continue.
