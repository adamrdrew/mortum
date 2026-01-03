#pragma once

#include <stdbool.h>

#include "assets/map_loader.h"
#include "game/notifications.h"
#include "game/player.h"
#include "game/sound_emitters.h"
#include "game/world.h"

typedef enum DoorsOpenResult {
	DOORS_OPENED = 0,
	DOORS_ALREADY_OPEN = 1,
	DOORS_NOT_FOUND = 2,
	DOORS_ON_COOLDOWN = 3,
	DOORS_MISSING_REQUIRED_ITEM = 4,
	DOORS_INVALID = 5,
} DoorsOpenResult;

typedef struct Door {
	char id[64];
	int wall_index;
	bool is_open;
	char closed_tex[64];
	char sound_open[64];
	char required_item[64];
	char required_item_missing_message[128];
	float next_allowed_s;
	float next_deny_toast_s;
} Door;

typedef struct Doors {
	Door* doors; // owned
	int door_count;
} Doors;

void doors_init(Doors* self);
void doors_destroy(Doors* self);

// Builds runtime doors from a map's door definitions.
// Mutates the bound walls' runtime door state (door_blocked + wall tex) based on starts_closed.
bool doors_build_from_map(Doors* self, World* world, const MapDoor* defs, int def_count);

int doors_count(const Doors* self);
const char* doors_id_at(const Doors* self, int index);

// Opens the nearest closed door in range of the player (open-only).
// Returns true only when a door transitions closed->open.
bool doors_try_open_near_player(
	Doors* self,
	World* world,
	const Player* player,
	Notifications* notifications,
	SoundEmitters* sfx,
	float listener_x,
	float listener_y,
	float now_s);

// Opens a door by ID (open-only). Uses the same gating/debounce/toast rules as in-world interaction.
DoorsOpenResult doors_try_open_by_id(
	Doors* self,
	World* world,
	const Player* player,
	Notifications* notifications,
	SoundEmitters* sfx,
	float listener_x,
	float listener_y,
	float now_s,
	const char* door_id);
