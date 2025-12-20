#pragma once

#include <stdbool.h>

#include "core/base.h"

typedef struct Entity {
	bool active;
	bool just_died;
	char type[32];
	float x;
	float y;
	float z;
	float vx;
	float vy;
	float radius;
	float cooldown_s;
	float lifetime_s;
	int health;
	int damage;
} Entity;

typedef struct EntityList {
	Entity* items; // owned
	int count;
	int capacity;
} EntityList;

void entity_init(Entity* e);
void entity_set_type(Entity* e, StringView type);

void entity_list_init(EntityList* list);
void entity_list_destroy(EntityList* list);
bool entity_list_push(EntityList* list, const Entity* e);
