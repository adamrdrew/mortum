#include "assets/asset_paths.h"
#include "assets/episode_loader.h"
#include "assets/map_loader.h"
#include "assets/scene_loader.h"
#include "core/log.h"
#include "platform/fs.h"
#include "platform/platform.h"

#include <stdbool.h>
#include <stdio.h>

static bool validate_episode_content(const AssetPaths* paths, const char* episode_filename) {
	Episode ep;
	if (!episode_load(&ep, paths, episode_filename)) {
		log_error("Failed to load episode: %s", episode_filename);
		return false;
	}

	bool ok = true;
	for (int i = 0; i < ep.enter_scene_count; i++) {
		log_info("Validating enter scene: %s", ep.enter_scenes[i]);
		Scene s;
		if (!scene_load(&s, paths, ep.enter_scenes[i])) {
			log_error("Failed to load scene: %s", ep.enter_scenes[i]);
			ok = false;
			break;
		}
		scene_destroy(&s);
	}
	for (int i = 0; ok && i < ep.exit_scene_count; i++) {
		log_info("Validating exit scene: %s", ep.exit_scenes[i]);
		Scene s;
		if (!scene_load(&s, paths, ep.exit_scenes[i])) {
			log_error("Failed to load scene: %s", ep.exit_scenes[i]);
			ok = false;
			break;
		}
		scene_destroy(&s);
	}

	for (int i = 0; i < ep.map_count; i++) {
		MapLoadResult map;
		log_info("Validating map: %s", ep.maps[i]);
		if (!map_load(&map, paths, ep.maps[i])) {
			log_error("Failed to load map: %s", ep.maps[i]);
			ok = false;
			break;
		}
		map_load_result_destroy(&map);
	}

	episode_destroy(&ep);
	return ok;
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
		// Default: validate boot.json (enter/exit scenes + maps).
		ok = validate_episode_content(&paths, "boot.json");
	}

	asset_paths_destroy(&paths);
	fs_paths_destroy(&fs);
	platform_shutdown();
	log_shutdown();
	return ok ? 0 : 2;
}
