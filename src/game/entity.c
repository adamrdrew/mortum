#include "game/entity.h"

#include <stdlib.h>
#include <string.h>

void entity_init(Entity* e) {
	memset(e, 0, sizeof(*e));
	e->active = true;
}

void entity_set_type(Entity* e, StringView type) {
	size_t n = type.len < 31 ? type.len : 31;
	memcpy(e->type, type.data, n);
	e->type[n] = '\0';
}

void entity_list_init(EntityList* list) {
	memset(list, 0, sizeof(*list));
}

void entity_list_destroy(EntityList* list) {
	free(list->items);
	memset(list, 0, sizeof(*list));
}

bool entity_list_push(EntityList* list, const Entity* e) {
	if (list->count >= list->capacity) {
		int new_cap = list->capacity == 0 ? 16 : list->capacity * 2;
		Entity* n = (Entity*)realloc(list->items, (size_t)new_cap * sizeof(Entity));
		if (!n) {
			return false;
		}
		list->items = n;
		list->capacity = new_cap;
	}
	list->items[list->count++] = *e;
	return true;
}
