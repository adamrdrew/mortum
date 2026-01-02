# Inventory System (Developer Documentation)

This document describes Mortum’s Inventory system: a minimal, deterministic set of item names (strings) owned by the player.

Source of truth:
- Public API: [include/game/inventory.h](../include/game/inventory.h)
- Implementation: [src/game/inventory.c](../src/game/inventory.c)
- Pickup integration: entity defs in [include/game/entities.h](../include/game/entities.h), JSON parsing in [src/game/entities.c](../src/game/entities.c), event application in [src/main.c](../src/main.c)
- Console commands: [src/game/console_commands.c](../src/game/console_commands.c)

## Goals / Constraints

- **Purely additive**: integrates with existing pickup flow without changing unrelated gameplay logic.
- **Deterministic**: stable iteration, linear search, stable compaction on removal.
- **Memory safe**: fixed-size storage, no allocations, no frees.
- **Set semantics**: no duplicates.
- **Capacity**: max 64 items.

## Data Model

- Inventory is a fixed-capacity array of fixed-size strings.
- Item names must be non-empty and shorter than 64 bytes.
- Ordering is insertion order (first time an item is added).

## Public API

Declared in [include/game/inventory.h](../include/game/inventory.h):

- `void inventory_init(Inventory* inv)`
- `void inventory_clear(Inventory* inv)`
- `uint32_t inventory_count(const Inventory* inv)`
- `const char* inventory_get(const Inventory* inv, uint32_t index)`
- `bool inventory_contains(const Inventory* inv, const char* item_name)`
- `bool inventory_add_item(Inventory* inv, const char* item_name)`
  - Returns `true` only when the item is newly added.
- `bool inventory_remove_item(Inventory* inv, const char* item_name)`
  - Returns `true` only when an item was removed.

## Player Ownership

- Inventory is stored on `Player` as `player.inventory`.
- It is reset as part of `player_init()` (new-game defaults).

## Pickups: add_to_inventory

Pickups can add an item string to the inventory.

### Entity def JSON

Example:

```json
{
  "name": "red_key",
  "kind": "pickup",
  "sprite": {
    "file": { "name": "red_key.png", "dimensions": {"x": 64, "y": 64} },
    "frames": { "count": 1, "dimensions": {"x": 64, "y": 64} },
    "scale": 1,
    "z_offset": 32
  },
  "radius": 0.35,
  "height": 0.6,
  "pickup": {
    "add_to_inventory": "red_key",
    "trigger_radius": 0.6,
    "pickup_sound": "Player_Jump.wav",
    "pickup_sound_gain": 1.0
  }
}
```

### Validation rules

For `kind: "pickup"`, the `pickup` object must specify exactly one of:

- `heal_amount`, or
- `ammo_type` + `ammo_amount`, or
- `add_to_inventory`

`add_to_inventory` must be a non-empty string shorter than 64 bytes.

### Runtime behavior

- Inventory pickup application happens when the main loop handles `ENTITY_EVENT_PLAYER_TOUCH`.
- The pickup is **consumed on touch**, even if the inventory already contains that item.

## Console Commands

- `inventory_list`
  - Prints a bracketed list. Prints `[]` if empty.
  - When non-empty, it prints `[` then each item on its own line, then `]`.
- `inventory_add <string>`
  - Prints `true` if newly added, else `false`.
- `inventory_remove <string>`
  - Prints `true` if removed, else `false`.
- `inventory_contains <string>`
  - Prints `true` if present, else `false`.
