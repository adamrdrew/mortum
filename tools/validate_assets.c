#include "assets/asset_paths.h"
#include "assets/menu_loader.h"
#include "assets/timeline_loader.h"
#include "assets/map_loader.h"
#include "assets/scene_loader.h"
#include "core/log.h"
#include "game/entities.h"
#include "platform/fs.h"
#include "platform/platform.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static bool validate_timeline_content_depth(const AssetPaths* paths, const EntityDefs* defs, const char* timeline_filename, int depth, const char* const* stack, int stack_count);

static bool validate_entity_defs(const AssetPaths* paths, EntityDefs* out_defs) {
	if (!paths || !out_defs) {
		return false;
	}
	entity_defs_init(out_defs);
	if (!entity_defs_load(out_defs, paths)) {
		log_error("Failed to load entity defs (Assets/Entities/entities.json)");
		entity_defs_destroy(out_defs);
		return false;
	}

	// Extra validation beyond parsing: ensure Shoot behaviors resolved projectile def references.
	for (uint32_t di = 0; di < out_defs->count; di++) {
		const EntityDef* d = &out_defs->defs[di];
		if (d->kind != ENTITY_KIND_ENEMY) {
			continue;
		}
		const EntityDefEnemy* ed = &d->u.enemy;
		if (!ed->states.enabled) {
			continue;
		}
		const EnemyBehaviorList* lists[] = {&ed->states.idle, &ed->states.engaged, &ed->states.attack, &ed->states.damaged, &ed->states.dying, &ed->states.dead};
		for (int li = 0; li < (int)(sizeof(lists) / sizeof(lists[0])); li++) {
			const EnemyBehaviorList* bl = lists[li];
			for (int bi = 0; bi < (int)bl->count; bi++) {
				const EnemyBehavior* b = &bl->behaviors[bi];
				if (b->type != ENEMY_BEHAVIOR_SHOOT) {
					continue;
				}
				const EnemyBehaviorShoot* sh = &b->u.shoot;
				if (sh->projectile_def_index == UINT32_MAX || sh->projectile_def_index >= out_defs->count) {
					log_error("entity def '%s' Shoot.projectile_def '%s' did not resolve", d->name, sh->projectile_def);
					entity_defs_destroy(out_defs);
					return false;
				}
				if (out_defs->defs[sh->projectile_def_index].kind != ENTITY_KIND_PROJECTILE) {
					log_error("entity def '%s' Shoot.projectile_def '%s' is not kind=projectile", d->name, sh->projectile_def);
					entity_defs_destroy(out_defs);
					return false;
				}
			}
		}
	}

	return true;
}

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

static bool validate_timeline_content_depth(const AssetPaths* paths, const EntityDefs* defs, const char* timeline_filename, int depth, const char* const* stack, int stack_count) {
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

	// Optional root-level pause menu.
	if (tl.pause_menu && tl.pause_menu[0] != '\0') {
		MenuAsset menu;
		log_info("Validating timeline pause_menu: %s", tl.pause_menu);
		if (!menu_load(&menu, paths, tl.pause_menu)) {
			log_error("Failed to load timeline pause_menu: %s", tl.pause_menu);
			timeline_destroy(&tl);
			return false;
		}
		menu_asset_destroy(&menu);
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
			if (defs && map.entity_count > 0) {
				for (int mi = 0; mi < map.entity_count; mi++) {
					const MapEntityPlacement* p = &map.entities[mi];
					if (p->sector < 0 || p->def_name[0] == '\0') {
						log_error("map '%s' entity[%d] has missing/unknown def/type and will be skipped at runtime", ev->name, mi);
						ok = false;
						break;
					}
					if (entity_defs_find(defs, p->def_name) == UINT32_MAX) {
						log_error("map '%s' entity[%d] def '%s' not found in entity defs", ev->name, mi, p->def_name);
						ok = false;
						break;
					}
				}
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
			if (!validate_timeline_content_depth(paths, defs, target, depth + 1, next_stack, next_count)) {
				ok = false;
				break;
			}
		}
	}

	timeline_destroy(&tl);
	return ok;
}

static bool validate_timeline_content(const AssetPaths* paths, const EntityDefs* defs, const char* timeline_filename) {
	return validate_timeline_content_depth(paths, defs, timeline_filename, 0, NULL, 0);
}

static bool validate_map(const AssetPaths* paths, const EntityDefs* defs, const char* map_filename) {
	MapLoadResult map;
	log_info("Validating map: %s", map_filename);
	if (!map_load(&map, paths, map_filename)) {
		log_error("Failed to load map: %s", map_filename);
		return false;
	}
	if (defs && map.entity_count > 0) {
		for (int mi = 0; mi < map.entity_count; mi++) {
			const MapEntityPlacement* p = &map.entities[mi];
			if (p->sector < 0 || p->def_name[0] == '\0') {
				log_error("map '%s' entity[%d] has missing/unknown def/type and will be skipped at runtime", map_filename, mi);
				map_load_result_destroy(&map);
				return false;
			}
			if (entity_defs_find(defs, p->def_name) == UINT32_MAX) {
				log_error("map '%s' entity[%d] def '%s' not found in entity defs", map_filename, mi, p->def_name);
				map_load_result_destroy(&map);
				return false;
			}
		}
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

	EntityDefs defs;
	if (!validate_entity_defs(&paths, &defs)) {
		asset_paths_destroy(&paths);
		fs_paths_destroy(&fs);
		platform_shutdown();
		log_shutdown();
		return 2;
	}

	bool ok = true;
	if (argc > 1) {
		// Validate each map filename argument (relative to Assets/Levels/).
		for (int i = 1; i < argc; i++) {
			if (!validate_map(&paths, &defs, argv[i])) {
				ok = false;
				break;
			}
		}
	} else {
		// Default: validate the timelines that drive normal gameplay.
		// boot.json covers core scenes/menus; episode_1.json covers map content.
		ok = validate_timeline_content(&paths, &defs, "boot.json");
		if (ok) {
			ok = validate_timeline_content(&paths, &defs, "episode_1.json");
		}
	}

	entity_defs_destroy(&defs);

	asset_paths_destroy(&paths);
	fs_paths_destroy(&fs);
	platform_shutdown();
	log_shutdown();
	return ok ? 0 : 2;
}
