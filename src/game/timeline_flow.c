#include "game/timeline_flow.h"

#include "assets/menu_loader.h"
#include "assets/scene_loader.h"
#include "core/crash_diag.h"
#include "core/log.h"

#include "game/map_music.h"
#include "game/menu_screen.h"
#include "game/scene_screen.h"
#include "game/level_start.h"

#include "game/doors.h"

#include "game/notifications.h"

#include "game/console_commands.h"

#include <string.h>

static void make_screen_ctx(const TimelineFlowRuntime* rt, ScreenContext* out) {
	memset(out, 0, sizeof(*out));
	out->fb = rt ? rt->fb : NULL;
	out->in = rt ? rt->in : NULL;
	out->paths = rt ? rt->paths : NULL;
	out->allow_input = rt ? rt->allow_scene_input : false;
	out->audio_enabled = rt ? rt->audio_enabled : false;
	out->music_enabled = rt ? rt->music_enabled : false;
}

bool timeline_flow_preserve_midi_on_scene_exit(const TimelineFlow* self) {
	return self && self->active && self->preserve_midi_on_scene_exit;
}

void timeline_flow_init(TimelineFlow* self) {
	if (!self) {
		return;
	}
	self->active = false;
	self->index = 0;
	self->preserve_midi_on_scene_exit = false;
}

bool timeline_flow_is_active(const TimelineFlow* self) {
	return self && self->active;
}

static bool scene_wants_no_stop_preserve(const TimelineFlowRuntime* rt, const char* scene_file) {
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

static bool try_start_scene(TimelineFlow* self, TimelineFlowRuntime* rt, const char* scene_file, bool preserve_midi_on_exit_for_current) {
	if (!rt || !rt->paths || !rt->fb || !rt->screens || !scene_file || scene_file[0] == '\0') {
		return false;
	}
	Scene scene;
	if (!scene_load(&scene, rt->paths, scene_file)) {
		log_warn("Timeline scene failed to load (treating as completed): %s", scene_file);
		return false;
	}
	Screen* scr = scene_screen_create(scene);
	if (!scr) {
		log_warn("Timeline scene failed to create screen (treating as completed): %s", scene_file);
		scene_destroy(&scene);
		return false;
	}
	ScreenContext sctx;
	make_screen_ctx(rt, &sctx);
	sctx.preserve_midi_on_exit = preserve_midi_on_exit_for_current;
	screen_runtime_set(rt->screens, scr, &sctx);
	if (self) {
		self->preserve_midi_on_scene_exit = preserve_midi_on_exit_for_current;
	}
	return true;
}

static bool try_load_map(TimelineFlowRuntime* rt, const char* map_name) {
	if (!rt || !rt->paths || !rt->timeline || !rt->map || !rt->map_ok || !rt->mesh || !rt->player || !rt->gs) {
		return false;
	}
	if (!rt->sfx_emitters || !rt->particle_emitters || !rt->entities || !rt->entity_defs) {
		return false;
	}
	if (!map_name || map_name[0] == '\0') {
		return false;
	}

	crash_diag_set_phase(PHASE_SCENE_TO_MAP_REQUEST);
	log_info_s("transition", "Timeline request: load map '%s'", map_name);

	// map_load() overwrites the MapLoadResult struct; destroy prior owned allocations first.
	if (*rt->map_ok) {
		log_info_s("transition", "Timeline destroying previous map");
		if (rt->doors) {
			doors_destroy(rt->doors);
		}
		map_load_result_destroy(rt->map);
		*rt->map_ok = false;
		if (rt->mesh) {
			level_mesh_destroy(rt->mesh);
			level_mesh_init(rt->mesh);
		}
	}

	crash_diag_set_phase(PHASE_MAP_LOAD_BEGIN);
	crash_diag_set_phase(PHASE_MAP_ASSETS_LOAD);
	bool ok = map_load(rt->map, rt->paths, map_name);
	*rt->map_ok = ok;
	if (!ok) {
		log_error("Timeline map failed to load (treating as completed): %s", map_name);
		return false;
	}
	crash_diag_set_phase(PHASE_MAP_INIT_WORLD);

	if (rt->map_name_buf && rt->map_name_cap > 0) {
		strncpy(rt->map_name_buf, map_name, rt->map_name_cap);
		rt->map_name_buf[rt->map_name_cap - 1] = '\0';
	}

	level_mesh_build(rt->mesh, &rt->map->world);
	if (rt->doors) {
		if (!doors_build_from_map(rt->doors, &rt->map->world, rt->map->doors, rt->map->door_count)) {
			log_error("Timeline doors failed to build (continuing without doors)");
		}
	}
	level_start_apply(rt->player, rt->map);
	rt->player->footstep_timer_s = 0.0f;

	crash_diag_set_phase(PHASE_MAP_SPAWN_ENTITIES_BEGIN);
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
	crash_diag_set_phase(PHASE_MAP_SPAWN_ENTITIES_END);

	rt->gs->mode = GAME_MODE_PLAYING;
	crash_diag_set_phase(PHASE_AUDIO_TRACK_SWITCH_BEGIN);
	game_map_music_maybe_start(rt->paths, rt->map, *rt->map_ok, rt->audio_enabled, rt->music_enabled, rt->prev_bgmusic, rt->prev_bgmusic_cap, rt->prev_soundfont, rt->prev_soundfont_cap);
	crash_diag_set_phase(PHASE_AUDIO_TRACK_SWITCH_END);
	return true;
}

static bool try_start_menu(TimelineFlowRuntime* rt, const char* menu_file) {
	if (!rt || !rt->paths || !rt->fb || !rt->screens || !rt->console_ctx || !menu_file || menu_file[0] == '\0') {
		return false;
	}
	MenuAsset menu;
	if (!menu_load(&menu, rt->paths, menu_file)) {
		log_warn("Timeline menu failed to load (treating as completed): %s", menu_file);
		return false;
	}
	Screen* scr = menu_screen_create(menu, true, rt->console_ctx);
	if (!scr) {
		log_warn("Timeline menu failed to create screen (treating as completed): %s", menu_file);
		menu_asset_destroy(&menu);
		return false;
	}
	ScreenContext sctx;
	make_screen_ctx(rt, &sctx);
	sctx.preserve_midi_on_exit = false;
	screen_runtime_set(rt->screens, scr, &sctx);
	return true;
}

static void flow_done(TimelineFlow* self, TimelineFlowRuntime* rt) {
	if (!self) {
		return;
	}
	self->active = false;
	self->preserve_midi_on_scene_exit = false;
	self->index = 0;
	if (rt && rt->using_timeline) {
		*rt->using_timeline = false;
	}
	// If no map is loaded, ensure we land in a safe non-playing mode.
	if (rt && rt->gs && (!rt->map_ok || !*rt->map_ok)) {
		rt->gs->mode = GAME_MODE_WIN;
	}
}

static void apply_on_complete(TimelineFlow* self, TimelineFlowRuntime* rt, const TimelineEvent* ev) {
	if (!self || !rt || !rt->timeline || !ev) {
		return;
	}
	switch (ev->on_complete) {
		case TIMELINE_ON_COMPLETE_ADVANCE:
			log_info_s("timeline", "on_complete=advance (idx %d -> %d)", self->index, self->index + 1);
			self->index++;
			break;
		case TIMELINE_ON_COMPLETE_LOOP:
			log_info_s("timeline", "on_complete=loop (idx %d -> 0)", self->index);
			self->index = 0;
			break;
		case TIMELINE_ON_COMPLETE_LOAD: {
			const char* target = ev->target ? ev->target : "";
			log_info_s("timeline", "on_complete=load target='%s'", target);
			// Load into the existing Timeline struct (owned by caller). Keep deterministic fallback on failure.
			Timeline next;
			if (!timeline_load(&next, rt->paths, target)) {
				log_error("Timeline failed to load target '%s'; falling back to advance-in-current", target);
				// Deterministic fallback: advance; if out-of-range, loop.
				int next_idx = self->index + 1;
				if (next_idx >= 0 && next_idx < rt->timeline->event_count) {
					self->index = next_idx;
				} else {
					self->index = 0;
				}
				break;
			}
			timeline_destroy(rt->timeline);
			*rt->timeline = next; // struct copy; takes ownership of allocations
			self->index = 0;
			if (rt->notifications) {
				notifications_clear_queue(rt->notifications);
			}
			log_info_s("timeline", "Loaded new timeline: name='%s' events=%d", rt->timeline->name ? rt->timeline->name : "(null)", rt->timeline->event_count);
		} break;
		default:
			self->index++;
			break;
	}
}

static const char* kind_to_string(TimelineEventKind k) {
	switch (k) {
		case TIMELINE_EVENT_SCENE: return "scene";
		case TIMELINE_EVENT_MAP: return "map";
		case TIMELINE_EVENT_MENU: return "menu";
		case TIMELINE_EVENT_COMMAND: return "command";
		default: return "?";
	}
}

static const char* on_complete_to_string(TimelineOnComplete oc) {
	switch (oc) {
		case TIMELINE_ON_COMPLETE_ADVANCE: return "advance";
		case TIMELINE_ON_COMPLETE_LOOP: return "loop";
		case TIMELINE_ON_COMPLETE_LOAD: return "load";
		default: return "?";
	}
}

static void flow_step(TimelineFlow* self, TimelineFlowRuntime* rt) {
	if (!self || !rt || !rt->timeline) {
		return;
	}
	if (rt->using_timeline) {
		*rt->using_timeline = true;
	}

	int guard = 0;
	while (self->active && guard++ < 1024) {
		if (rt->timeline->event_count <= 0) {
			log_warn("Timeline has zero events; stopping flow");
			flow_done(self, rt);
			return;
		}
		if (self->index < 0 || self->index >= rt->timeline->event_count) {
			log_warn("Timeline index out of range (idx=%d events=%d); stopping flow", self->index, rt->timeline->event_count);
			flow_done(self, rt);
			return;
		}

		TimelineEvent* ev = &rt->timeline->events[self->index];
		if (rt->notifications) {
			notifications_clear_queue(rt->notifications);
		}
		log_info_s(
			"timeline",
			"Starting event: idx=%d kind=%s name='%s' on_complete=%s",
			self->index,
			kind_to_string(ev->kind),
			ev->name ? ev->name : "",
			on_complete_to_string(ev->on_complete)
		);

		if (ev->kind == TIMELINE_EVENT_SCENE) {
			bool preserve_on_exit = false;
			int next_idx = self->index + 1;
			if (next_idx >= 0 && next_idx < rt->timeline->event_count) {
				TimelineEvent* next = &rt->timeline->events[next_idx];
				if (next->kind == TIMELINE_EVENT_SCENE && next->name) {
					preserve_on_exit = scene_wants_no_stop_preserve(rt, next->name);
				}
			}
			if (try_start_scene(self, rt, ev->name, preserve_on_exit)) {
				return;
			}
			// Load failed: treat as completed immediately.
			apply_on_complete(self, rt, ev);
			self->preserve_midi_on_scene_exit = false;
			continue;
		}

		if (ev->kind == TIMELINE_EVENT_MAP) {
			self->preserve_midi_on_scene_exit = false;
			if (try_load_map(rt, ev->name)) {
				return;
			}
			// Load failed: treat as completed immediately.
			apply_on_complete(self, rt, ev);
			continue;
		}

		if (ev->kind == TIMELINE_EVENT_MENU) {
			self->preserve_midi_on_scene_exit = false;
			if (try_start_menu(rt, ev->name)) {
				return;
			}
			// Load failed: treat as completed immediately.
			apply_on_complete(self, rt, ev);
			continue;
		}

		if (ev->kind == TIMELINE_EVENT_COMMAND) {
			self->preserve_midi_on_scene_exit = false;
			bool ok = false;
			if (rt->con && rt->console_ctx && ev->name && ev->name[0] != '\0') {
				ok = console_execute_line(rt->con, ev->name, rt->console_ctx);
			} else {
				log_warn("Timeline command event missing console/context/name; treating as completed");
			}
			if (!ok) {
				log_warn("Timeline command failed or unknown (treating as completed): %s", ev->name ? ev->name : "");
			}
			apply_on_complete(self, rt, ev);
			continue;
		}

		// Unknown kind: skip safely.
		log_error("Timeline event has unknown kind; skipping");
		apply_on_complete(self, rt, ev);
	}

	if (guard >= 1024) {
		log_error("Timeline flow exceeded transition guard; stopping to avoid lockup");
		flow_done(self, rt);
	}
}

bool timeline_flow_start(TimelineFlow* self, TimelineFlowRuntime* rt) {
	if (!self || !rt || !rt->paths || !rt->timeline) {
		return false;
	}
	self->active = true;
	self->index = 0;
	self->preserve_midi_on_scene_exit = false;
	flow_step(self, rt);
	return self->active;
}

static bool current_event_is_kind(const TimelineFlow* self, const Timeline* tl, TimelineEventKind k) {
	if (!self || !tl || !self->active) {
		return false;
	}
	if (self->index < 0 || self->index >= tl->event_count) {
		return false;
	}
	return tl->events && tl->events[self->index].kind == k;
}


void timeline_flow_on_screen_completed(TimelineFlow* self, TimelineFlowRuntime* rt) {
	if (!self || !rt || !rt->timeline || !self->active) {
		return;
	}
	if (!current_event_is_kind(self, rt->timeline, TIMELINE_EVENT_SCENE) && !current_event_is_kind(self, rt->timeline, TIMELINE_EVENT_MENU)) {
		return;
	}
	TimelineEvent* ev = &rt->timeline->events[self->index];
	log_info_s("timeline", "Screen completed: idx=%d kind=%s name='%s'", self->index, kind_to_string(ev->kind), ev->name ? ev->name : "");
	apply_on_complete(self, rt, ev);
	self->preserve_midi_on_scene_exit = false;
	flow_step(self, rt);
}

void timeline_flow_on_scene_completed(TimelineFlow* self, TimelineFlowRuntime* rt) {
	timeline_flow_on_screen_completed(self, rt);
}

void timeline_flow_on_map_win(TimelineFlow* self, TimelineFlowRuntime* rt) {
	if (!self || !rt || !rt->timeline || !self->active) {
		return;
	}
	if (!current_event_is_kind(self, rt->timeline, TIMELINE_EVENT_MAP)) {
		return;
	}
	TimelineEvent* ev = &rt->timeline->events[self->index];
	log_info_s("timeline", "Map win: idx=%d name='%s'", self->index, ev->name ? ev->name : "");
	apply_on_complete(self, rt, ev);
	flow_step(self, rt);
}

void timeline_flow_abort(TimelineFlow* self) {
	if (!self) {
		return;
	}
	self->active = false;
	self->index = 0;
	self->preserve_midi_on_scene_exit = false;
}
