#pragma once

#include <stdbool.h>

#include "assets/asset_paths.h"

typedef enum TimelineEventKind {
	TIMELINE_EVENT_SCENE = 0,
	TIMELINE_EVENT_MAP = 1,
	TIMELINE_EVENT_MENU = 2,
} TimelineEventKind;

typedef enum TimelineOnComplete {
	TIMELINE_ON_COMPLETE_ADVANCE = 0,
	TIMELINE_ON_COMPLETE_LOOP = 1,
	TIMELINE_ON_COMPLETE_LOAD = 2,
} TimelineOnComplete;

typedef struct TimelineEvent {
	TimelineEventKind kind;
	TimelineOnComplete on_complete;
	char* name;   // owned
	char* target; // owned; only used for on_complete==LOAD
} TimelineEvent;

typedef struct Timeline {
	char* name; // owned
	// Optional menu filename (safe .json filename under Assets/Menus) opened by the pause/menu keybinding.
	char* pause_menu; // owned; optional
	TimelineEvent* events; // owned array
	int event_count;
} Timeline;

void timeline_destroy(Timeline* self);

// Loads a timeline from Assets/Timelines/<timeline_filename>.
// This function overwrites `*out` with a fresh zeroed struct; destroy any prior contents first.
bool timeline_load(Timeline* out, const AssetPaths* paths, const char* timeline_filename);
