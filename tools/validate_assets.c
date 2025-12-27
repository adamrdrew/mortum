#include "assets/asset_paths.h"
#include "assets/menu_loader.h"
#include "assets/timeline_loader.h"
#include "assets/map_loader.h"
#include "assets/scene_loader.h"
#include "core/log.h"
#include "platform/fs.h"
#include "platform/platform.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool validate_timeline_content_depth(const AssetPaths* paths, const char* timeline_filename, int depth, const char* const* stack, int stack_count);

static bool stack_contains(const char* const* stack, int stack_count, const char* name) {
	if (!stack || stack_count <= 0 || !name) {
		return false;
	}
	for (int i = 0; i < stack_count; i++) {
		if (stack[i] && strcmp(stack[i], name) == 0) {
			return true;
		}
	}
	return false;
}

static bool validate_timeline_content_depth(const AssetPaths* paths, const char* timeline_filename, int depth, const char* const* stack, int stack_count) {
	if (!paths || !timeline_filename) {
		return false;
	}
	if (depth > 8) {
		log_warn("Timeline validation depth exceeded; skipping deeper validation at: %s", timeline_filename);
		return true;
	}
	if (stack_contains(stack, stack_count, timeline_filename)) {
		log_warn("Timeline validation cycle detected; skipping deeper validation at: %s", timeline_filename);
		return true;
	}

	Timeline tl;
	if (!timeline_load(&tl, paths, timeline_filename)) {
		log_error("Failed to load timeline: %s", timeline_filename);
		return false;
	}

	bool ok = true;
	for (int i = 0; i < tl.event_count; i++) {
		TimelineEvent* ev = &tl.events[i];
		if (!ev || !ev->name || ev->name[0] == '\0') {
			log_error("Timeline event[%d] missing name", i);
			ok = false;
			break;
		}
		if (ev->kind == TIMELINE_EVENT_SCENE) {
			log_info("Validating scene: %s", ev->name);
			Scene s;
			if (!scene_load(&s, paths, ev->name)) {
				log_error("Failed to load scene: %s", ev->name);
				ok = false;
				break;
			}
			scene_destroy(&s);
		} else if (ev->kind == TIMELINE_EVENT_MAP) {
			MapLoadResult map;
			log_info("Validating map: %s", ev->name);
			if (!map_load(&map, paths, ev->name)) {
				log_error("Failed to load map: %s", ev->name);
				ok = false;
				break;
			}
			map_load_result_destroy(&map);
		} else if (ev->kind == TIMELINE_EVENT_MENU) {
			MenuAsset menu;
			log_info("Validating menu: %s", ev->name);
			if (!menu_load(&menu, paths, ev->name)) {
				log_error("Failed to load menu: %s", ev->name);
				ok = false;
				break;
			}
			menu_asset_destroy(&menu);
		}

		if (ev->on_complete == TIMELINE_ON_COMPLETE_LOAD) {
			const char* target = ev->target ? ev->target : "";
			if (target[0] == '\0') {
				log_error("Timeline event[%d] on_complete=load missing target", i);
				ok = false;
				break;
			}
			log_info("Validating timeline target: %s", target);
			const char* next_stack[16];
			int next_count = 0;
			for (int j = 0; j < stack_count && j < (int)(sizeof(next_stack) / sizeof(next_stack[0])) - 1; j++) {
				next_stack[next_count++] = stack[j];
			}
			next_stack[next_count++] = timeline_filename;
			if (!validate_timeline_content_depth(paths, target, depth + 1, next_stack, next_count)) {
				ok = false;
				break;
			}
		}
	}

	timeline_destroy(&tl);
	return ok;
}

static bool validate_timeline_content(const AssetPaths* paths, const char* timeline_filename) {
	return validate_timeline_content_depth(paths, timeline_filename, 0, NULL, 0);
}

static bool validate_map(const AssetPaths* paths, const char* map_filename) {
	MapLoadResult map;
	log_info("Validating map: %s", map_filename);
	if (!map_load(&map, paths, map_filename)) {
		log_error("Failed to load map: %s", map_filename);
		return false;
	}
	if (map.world.light_count > 0) {
		log_info("Map %s: %d point lights", map_filename, map.world.light_count);
	}
	map_load_result_destroy(&map);
	return true;
}

int main(int argc, char** argv) {
	if (!log_init(LOG_LEVEL_INFO)) {
		return 1;
	}

	PlatformConfig pcfg;
	pcfg.enable_audio = false;
	if (!platform_init(&pcfg)) {
		log_shutdown();
		return 1;
	}

	FsPaths fs;
	if (!fs_paths_init(&fs, "mortum", "mortum")) {
		platform_shutdown();
		log_shutdown();
		return 1;
	}

	AssetPaths paths;
	asset_paths_init(&paths, fs.base_path);

	bool ok = true;
	if (argc > 1) {
		// Validate each map filename argument (relative to Assets/Levels/).
		for (int i = 1; i < argc; i++) {
			if (!validate_map(&paths, argv[i])) {
				ok = false;
				break;
			}
		}
	} else {
		// Default: validate boot.json timeline (events).
		ok = validate_timeline_content(&paths, "boot.json");
	}

	asset_paths_destroy(&paths);
	fs_paths_destroy(&fs);
	platform_shutdown();
	log_shutdown();
	return ok ? 0 : 2;
}
