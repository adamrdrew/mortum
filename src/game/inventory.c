#include "game/inventory.h"

#include <string.h>

typedef struct InventoryImpl {
	uint32_t count;
	char items[INVENTORY_MAX_ITEMS][INVENTORY_ITEM_MAX];
} InventoryImpl;

_Static_assert(sizeof(Inventory) == sizeof(InventoryImpl), "Inventory storage size mismatch");

static InventoryImpl* inv_impl(Inventory* inv) {
	return (InventoryImpl*)(void*)inv->_storage;
}

static const InventoryImpl* inv_impl_const(const Inventory* inv) {
	return (const InventoryImpl*)(const void*)inv->_storage;
}

static bool inventory_name_valid(const char* s) {
	if (!s || s[0] == '\0') {
		return false;
	}
	size_t n = strlen(s);
	// Enforce < INVENTORY_ITEM_MAX so we never truncate (and stay deterministic).
	return n > 0 && n < INVENTORY_ITEM_MAX;
}

void inventory_init(Inventory* inv) {
	if (!inv) {
		return;
	}
	InventoryImpl* impl = inv_impl(inv);
	impl->count = 0u;
	memset(impl->items, 0, sizeof(impl->items));
}

void inventory_clear(Inventory* inv) {
	inventory_init(inv);
}

uint32_t inventory_count(const Inventory* inv) {
	if (!inv) {
		return 0u;
	}
	return inv_impl_const(inv)->count;
}

const char* inventory_get(const Inventory* inv, uint32_t index) {
	if (!inv) {
		return NULL;
	}
	const InventoryImpl* impl = inv_impl_const(inv);
	if (index >= impl->count) {
		return NULL;
	}
	return impl->items[index];
}

bool inventory_contains(const Inventory* inv, const char* item_name) {
	if (!inv || !inventory_name_valid(item_name)) {
		return false;
	}
	const InventoryImpl* impl = inv_impl_const(inv);
	for (uint32_t i = 0u; i < impl->count; i++) {
		if (strcmp(impl->items[i], item_name) == 0) {
			return true;
		}
	}
	return false;
}

bool inventory_add_item(Inventory* inv, const char* item_name) {
	if (!inv || !inventory_name_valid(item_name)) {
		return false;
	}
	InventoryImpl* impl = inv_impl(inv);
	for (uint32_t i = 0u; i < impl->count; i++) {
		if (strcmp(impl->items[i], item_name) == 0) {
			return false;
		}
	}
	if (impl->count >= (uint32_t)INVENTORY_MAX_ITEMS) {
		return false;
	}
	strncpy(impl->items[impl->count], item_name, INVENTORY_ITEM_MAX - 1);
	impl->items[impl->count][INVENTORY_ITEM_MAX - 1] = '\0';
	impl->count++;
	return true;
}

bool inventory_remove_item(Inventory* inv, const char* item_name) {
	if (!inv || !inventory_name_valid(item_name)) {
		return false;
	}
	InventoryImpl* impl = inv_impl(inv);
	uint32_t idx = UINT32_MAX;
	for (uint32_t i = 0u; i < impl->count; i++) {
		if (strcmp(impl->items[i], item_name) == 0) {
			idx = i;
			break;
		}
	}
	if (idx == UINT32_MAX) {
		return false;
	}
	// Stable compaction.
	if (idx + 1u < impl->count) {
		memmove(
			&impl->items[idx],
			&impl->items[idx + 1u],
			(size_t)(impl->count - (idx + 1u)) * sizeof(impl->items[0])
		);
	}
	impl->count--;
	impl->items[impl->count][0] = '\0';
	return true;
}
