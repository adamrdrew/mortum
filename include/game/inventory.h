#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdalign.h>

// Simple fixed-capacity inventory set (no duplicates).
//
// Design goals:
// - Deterministic: linear scans, stable ordering.
// - Allocation-free: no malloc/free.
// - Encapsulated storage: callers interact via functions only.

#define INVENTORY_MAX_ITEMS 64
#define INVENTORY_ITEM_MAX 64

// Storage layout is intentionally opaque.
// Internally this is { uint32_t count; char items[64][64]; }.
#define INVENTORY_STORAGE_BYTES (sizeof(uint32_t) + (size_t)INVENTORY_MAX_ITEMS * (size_t)INVENTORY_ITEM_MAX)

typedef struct Inventory {
	_Alignas(uint32_t) uint8_t _storage[INVENTORY_STORAGE_BYTES];
} Inventory;

void inventory_init(Inventory* inv);
void inventory_clear(Inventory* inv);

uint32_t inventory_count(const Inventory* inv);

// Returns NULL if index is out of range.
const char* inventory_get(const Inventory* inv, uint32_t index);

bool inventory_contains(const Inventory* inv, const char* item_name);

// Returns true if a new item was added.
// Returns false if the item is already present, invalid, or inventory is full.
bool inventory_add_item(Inventory* inv, const char* item_name);

// Returns true if an item was removed.
// Returns false if missing or invalid.
bool inventory_remove_item(Inventory* inv, const char* item_name);
