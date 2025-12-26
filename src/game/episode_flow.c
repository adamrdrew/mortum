#include "game/episode_flow.h"

#include "assets/scene_loader.h"
#include "assets/midi_player.h"
#include "core/log.h"

#include "game/map_music.h"
#include "game/scene_screen.h"

#include <string.h>

static void make_screen_ctx(const EpisodeFlowRuntime* rt, ScreenContext* out) {
	memset(out, 0, sizeof(*out));
	out->fb = rt ? rt->fb : NULL;
	out->in = rt ? rt->in : NULL;
	out->paths = rt ? rt->paths : NULL;
	out->allow_input = rt ? rt->allow_scene_input : false;
	out->audio_enabled = rt ? rt->audio_enabled : false;
	out->music_enabled = rt ? rt->music_enabled : false;
}

bool episode_flow_preserve_midi_on_scene_exit(const EpisodeFlow* self) {
	return self && self->active && self->preserve_midi_on_scene_exit;
}

static bool scene_wants_no_stop_preserve(const EpisodeFlowRuntime* rt, const char* scene_file) {
	if (!rt || !rt->paths || !scene_file || scene_file[0] == '\0') {
		return false;
	}
	Scene s;
	if (!scene_load(&s, rt->paths, scene_file)) {
		return false;
	}
	bool preserve = s.music.no_stop && (!s.music.midi_file || s.music.midi_file[0] == '\0');
	scene_destroy(&s);
	return preserve;
}

static bool try_start_scene(EpisodeFlow* self, EpisodeFlowRuntime* rt, const char* scene_file, bool preserve_midi_on_exit_for_current) {
	if (!rt || !rt->paths || !rt->fb || !rt->screens || !scene_file || scene_file[0] == '\0') {
		return false;
	}
	Scene scene;
	if (!scene_load(&scene, rt->paths, scene_file)) {
		log_warn("Episode scene failed to load (skipping): %s", scene_file);
		return false;
	}
	bool preserve_midi = scene.music.no_stop && (!scene.music.midi_file || scene.music.midi_file[0] == '\0');
	Screen* scr = scene_screen_create(scene);
	if (!scr) {
		log_warn("Episode scene failed to create screen (skipping): %s", scene_file);
		scene_destroy(&scene);
		return false;
	}
	ScreenContext sctx;
	make_screen_ctx(rt, &sctx);
	sctx.preserve_midi_on_exit = preserve_midi;
	screen_runtime_set(rt->screens, scr, &sctx);
	if (self) {
		self->preserve_midi_on_scene_exit = preserve_midi_on_exit_for_current;
	}
	return true;
}

static bool try_load_current_map(EpisodeFlowRuntime* rt) {
	if (!rt || !rt->paths || !rt->ep || !rt->runner || !rt->map || !rt->map_ok || !rt->mesh || !rt->player || !rt->gs) {
		return false;
	}
	// These are required by the respawn/reset section below.
	if (!rt->sfx_emitters || !rt->particle_emitters || !rt->entities || !rt->entity_defs) {
		return false;
	}
	const char* map_name = episode_runner_current_map(rt->runner, rt->ep);
	if (!map_name || map_name[0] == '\0') {
		return false;
	}

	// map_load() overwrites the MapLoadResult struct; destroy prior owned allocations first.
	if (*rt->map_ok) {
		map_load_result_destroy(rt->map);
		*rt->map_ok = false;
		if (rt->mesh) {
			level_mesh_destroy(rt->mesh);
			level_mesh_init(rt->mesh);
		}
	}

	bool ok = map_load(rt->map, rt->paths, map_name);
	*rt->map_ok = ok;
	if (!ok) {
		log_error("Episode map failed to load: %s", map_name);
		return false;
	}

	if (rt->map_name_buf && rt->map_name_cap > 0) {
		strncpy(rt->map_name_buf, map_name, rt->map_name_cap);
		rt->map_name_buf[rt->map_name_cap - 1] = '\0';
	}

	level_mesh_build(rt->mesh, &rt->map->world);
	episode_runner_apply_level_start(rt->player, rt->map);
	rt->player->footstep_timer_s = 0.0f;

	// Respawn map-authored systems.
	sound_emitters_reset(rt->sfx_emitters);
	sound_emitters_set_enabled(rt->sfx_emitters, rt->audio_enabled && rt->sound_emitters_enabled);
	entity_system_reset(rt->entities, &rt->map->world, rt->particle_emitters, rt->entity_defs);
	particle_emitters_reset(rt->particle_emitters);
	if (rt->map->sounds && rt->map->sound_count > 0) {
		for (int i = 0; i < rt->map->sound_count; i++) {
			MapSoundEmitter* ms = &rt->map->sounds[i];
			SoundEmitterId id = sound_emitter_create(rt->sfx_emitters, ms->x, ms->y, ms->spatial, ms->gain);
			if (ms->loop) {
				sound_emitter_start_loop(rt->sfx_emitters, id, ms->sound, rt->player->body.x, rt->player->body.y);
			}
		}
	}
	if (rt->map->particles && rt->map->particle_count > 0) {
		for (int i = 0; i < rt->map->particle_count; i++) {
			MapParticleEmitter* mp = &rt->map->particles[i];
			(void)particle_emitter_create(rt->particle_emitters, &rt->map->world, mp->x, mp->y, mp->z, &mp->def);
		}
	}
	if (rt->map->entities && rt->map->entity_count > 0) {
		entity_system_spawn_map(rt->entities, rt->map->entities, rt->map->entity_count);
	}

	rt->gs->mode = GAME_MODE_PLAYING;
	game_map_music_maybe_start(rt->paths, rt->map, *rt->map_ok, rt->audio_enabled, rt->music_enabled, rt->prev_bgmusic, rt->prev_bgmusic_cap,
			rt->prev_soundfont, rt->prev_soundfont_cap);
	return true;
}

static void flow_step(EpisodeFlow* self, EpisodeFlowRuntime* rt) {
	if (!self || !rt || !rt->ep) {
		return;
	}
	// Prevent runaway loops if content is totally broken.
	int guard = 0;
	while (self->active && guard++ < 1024) {
		switch (self->phase) {
			case EPISODE_FLOW_PHASE_ENTER_SCENES: {
				int n = rt->ep->enter_scene_count;
				while (self->enter_index < n) {
					const char* s = rt->ep->enter_scenes ? rt->ep->enter_scenes[self->enter_index] : NULL;
					self->enter_index++;
					bool preserve_on_exit = false;
					if (self->enter_index < n) {
						const char* next = rt->ep->enter_scenes ? rt->ep->enter_scenes[self->enter_index] : NULL;
						preserve_on_exit = scene_wants_no_stop_preserve(rt, next);
					}
					if (try_start_scene(self, rt, s, preserve_on_exit)) {
						return;
					}
				}
				self->phase = EPISODE_FLOW_PHASE_MAPS;
				self->preserve_midi_on_scene_exit = false;
			} break;

			case EPISODE_FLOW_PHASE_MAPS: {
				if (rt->ep->map_count <= 0) {
					self->phase = EPISODE_FLOW_PHASE_EXIT_SCENES;
					break;
				}
				if (!try_load_current_map(rt)) {
					// Required failure behavior: end episode gracefully.
					self->phase = EPISODE_FLOW_PHASE_EXIT_SCENES;
					break;
				}
				return;
			} break;

			case EPISODE_FLOW_PHASE_EXIT_SCENES: {
				int n = rt->ep->exit_scene_count;
				while (self->exit_index < n) {
					const char* s = rt->ep->exit_scenes ? rt->ep->exit_scenes[self->exit_index] : NULL;
					self->exit_index++;
					bool preserve_on_exit = false;
					if (self->exit_index < n) {
						const char* next = rt->ep->exit_scenes ? rt->ep->exit_scenes[self->exit_index] : NULL;
						preserve_on_exit = scene_wants_no_stop_preserve(rt, next);
					}
					if (try_start_scene(self, rt, s, preserve_on_exit)) {
						return;
					}
				}
				self->phase = EPISODE_FLOW_PHASE_DONE;
				self->preserve_midi_on_scene_exit = false;
			} break;

			case EPISODE_FLOW_PHASE_DONE: {
				self->active = false;
				self->preserve_midi_on_scene_exit = false;
				if (rt->using_episode) {
					*rt->using_episode = false;
				}
				// If no map is loaded, ensure we land in a safe non-playing mode.
				if (rt->gs && (!rt->map_ok || !*rt->map_ok)) {
					rt->gs->mode = GAME_MODE_WIN;
				}
				return;
			} break;
		}
	}
	if (guard >= 1024) {
		log_error("EpisodeFlow: exceeded step guard (content loop?)");
		self->active = false;
		if (rt->using_episode) {
			*rt->using_episode = false;
		}
	}
}

void episode_flow_init(EpisodeFlow* self) {
	if (!self) {
		return;
	}
	self->active = false;
	self->phase = EPISODE_FLOW_PHASE_DONE;
	self->enter_index = 0;
	self->exit_index = 0;
	self->preserve_midi_on_scene_exit = false;
}

bool episode_flow_start(EpisodeFlow* self, EpisodeFlowRuntime* rt) {
	if (!self || !rt || !rt->ep) {
		return false;
	}
	self->active = true;
	self->enter_index = 0;
	self->exit_index = 0;
	self->preserve_midi_on_scene_exit = false;

	if (rt->using_episode) {
		*rt->using_episode = true;
	}
	if (rt->runner) {
		episode_runner_init(rt->runner);
		(void)episode_runner_start(rt->runner, rt->ep);
	}

	if (rt->ep->enter_scene_count > 0) {
		self->phase = EPISODE_FLOW_PHASE_ENTER_SCENES;
	} else if (rt->ep->map_count > 0) {
		self->phase = EPISODE_FLOW_PHASE_MAPS;
	} else if (rt->ep->exit_scene_count > 0) {
		self->phase = EPISODE_FLOW_PHASE_EXIT_SCENES;
	} else {
		self->phase = EPISODE_FLOW_PHASE_DONE;
	}

	flow_step(self, rt);
	return true;
}

void episode_flow_on_scene_completed(EpisodeFlow* self, EpisodeFlowRuntime* rt) {
	if (!self || !self->active) {
		return;
	}
	if (self->phase != EPISODE_FLOW_PHASE_ENTER_SCENES && self->phase != EPISODE_FLOW_PHASE_EXIT_SCENES) {
		return;
	}
	flow_step(self, rt);
}

void episode_flow_on_map_win(EpisodeFlow* self, EpisodeFlowRuntime* rt) {
	if (!self || !rt || !self->active || self->phase != EPISODE_FLOW_PHASE_MAPS) {
		return;
	}
	if (!rt->runner || !rt->ep) {
		self->phase = EPISODE_FLOW_PHASE_EXIT_SCENES;
		flow_step(self, rt);
		return;
	}
	if (episode_runner_advance(rt->runner, rt->ep)) {
		// Next map.
		if (!try_load_current_map(rt)) {
			self->phase = EPISODE_FLOW_PHASE_EXIT_SCENES;
			flow_step(self, rt);
		}
		return;
	}
	// End of maps.
	self->phase = EPISODE_FLOW_PHASE_EXIT_SCENES;
	flow_step(self, rt);
}

void episode_flow_cancel(EpisodeFlow* self) {
	if (!self) {
		return;
	}
	self->active = false;
	self->phase = EPISODE_FLOW_PHASE_DONE;
	self->enter_index = 0;
	self->exit_index = 0;
	self->preserve_midi_on_scene_exit = false;
}
