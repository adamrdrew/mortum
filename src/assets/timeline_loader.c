#include "assets/timeline_loader.h"

#include "assets/json.h"
#include "core/log.h"
#include "core/path_safety.h"

#include <ctype.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static char* sv_dup(StringView s) {
	char* out = (char*)malloc(s.len + 1);
	if (!out) {
		return NULL;
	}
	memcpy(out, s.data, s.len);
	out[s.len] = '\0';
	return out;
}

static bool ends_with_ci(const char* s, const char* suffix) {
	if (!s || !suffix) {
		return false;
	}
	size_t ns = strlen(s);
	size_t nf = strlen(suffix);
	if (nf > ns) {
		return false;
	}
	const char* tail = s + (ns - nf);
	for (size_t i = 0; i < nf; i++) {
		char a = (char)tolower((unsigned char)tail[i]);
		char b = (char)tolower((unsigned char)suffix[i]);
		if (a != b) {
			return false;
		}
	}
	return true;
}

// Stricter than name_is_safe_relpath: disallows any path separators.
static bool name_is_safe_filename(const char* name) {
	if (!name || !name[0]) {
		return false;
	}
	if (strstr(name, "..") != NULL) {
		return false;
	}
	for (const char* p = name; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '/' || c == '\\') {
			return false;
		}
		if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) {
			return false;
		}
	}
	return true;
}

static void timeline_event_destroy(TimelineEvent* ev) {
	if (!ev) {
		return;
	}
	free(ev->name);
	free(ev->target);
	ev->name = NULL;
	ev->target = NULL;
}

void timeline_destroy(Timeline* self) {
	if (!self) {
		return;
	}
	free(self->name);
	self->name = NULL;
	free(self->pause_menu);
	self->pause_menu = NULL;
	if (self->events) {
		for (int i = 0; i < self->event_count; i++) {
			timeline_event_destroy(&self->events[i]);
		}
		free(self->events);
	}
	self->events = NULL;
	self->event_count = 0;
}

static bool parse_event_kind(const char* s, TimelineEventKind* out) {
	if (!s || !out) {
		return false;
	}
	if (strcmp(s, "scene") == 0) {
		*out = TIMELINE_EVENT_SCENE;
		return true;
	}
	if (strcmp(s, "map") == 0) {
		*out = TIMELINE_EVENT_MAP;
		return true;
	}
	if (strcmp(s, "menu") == 0) {
		*out = TIMELINE_EVENT_MENU;
		return true;
	}
	return false;
}

static bool parse_on_complete(const char* s, TimelineOnComplete* out) {
	if (!s || !out) {
		return false;
	}
	if (strcmp(s, "advance") == 0) {
		*out = TIMELINE_ON_COMPLETE_ADVANCE;
		return true;
	}
	if (strcmp(s, "loop") == 0) {
		*out = TIMELINE_ON_COMPLETE_LOOP;
		return true;
	}
	if (strcmp(s, "load") == 0) {
		*out = TIMELINE_ON_COMPLETE_LOAD;
		return true;
	}
	return false;
}

bool timeline_load(Timeline* out, const AssetPaths* paths, const char* timeline_filename) {
	if (!out) {
		return false;
	}
	memset(out, 0, sizeof(*out));
	if (!paths || !timeline_filename || timeline_filename[0] == '\0') {
		log_error("Timeline load: missing filename");
		return false;
	}
	if (!name_is_safe_relpath(timeline_filename) || !ends_with_ci(timeline_filename, ".json")) {
		log_error("Timeline filename must be a safe relative .json path under Assets/Timelines: %s", timeline_filename);
		return false;
	}
	char* full = asset_path_join(paths, "Timelines", timeline_filename);
	if (!full) {
		return false;
	}

	JsonDoc doc;
	if (!json_doc_load_file(&doc, full)) {
		free(full);
		return false;
	}
	free(full);

	if (doc.token_count < 1 || !json_token_is_object(&doc, 0)) {
		log_error("Timeline JSON root must be an object");
		json_doc_destroy(&doc);
		return false;
	}

	int t_name = -1;
	int t_events = -1;
	int t_pause_menu = -1;
	if (!json_object_get(&doc, 0, "name", &t_name) || !json_object_get(&doc, 0, "events", &t_events)) {
		log_error("Timeline JSON missing required fields: name, events");
		json_doc_destroy(&doc);
		return false;
	}
	(void)json_object_get(&doc, 0, "pause_menu", &t_pause_menu);

	StringView sv_name;
	if (!json_get_string(&doc, t_name, &sv_name) || sv_name.len <= 0) {
		log_error("Timeline name must be a non-empty string");
		json_doc_destroy(&doc);
		return false;
	}
	if (!json_token_is_array(&doc, t_events)) {
		log_error("Timeline events must be an array");
		json_doc_destroy(&doc);
		return false;
	}

	out->name = sv_dup(sv_name);
	if (!out->name) {
		timeline_destroy(out);
		json_doc_destroy(&doc);
		return false;
	}

	// Optional pause_menu.
	if (t_pause_menu >= 0) {
		StringView sv_pause;
		if (!json_get_string(&doc, t_pause_menu, &sv_pause) || sv_pause.len <= 0) {
			log_error("Timeline pause_menu must be a non-empty string when present");
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		out->pause_menu = sv_dup(sv_pause);
		if (!out->pause_menu) {
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		if (!name_is_safe_filename(out->pause_menu) || !ends_with_ci(out->pause_menu, ".json")) {
			log_error("Timeline pause_menu must be a safe .json filename under Assets/Menus (no slashes): %s", out->pause_menu);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}

	int n = json_array_size(&doc, t_events);
	if (n < 0) {
		n = 0;
	}
	out->event_count = n;
	out->events = NULL;
	if (n > 0) {
		out->events = (TimelineEvent*)calloc((size_t)n, sizeof(TimelineEvent));
		if (!out->events) {
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
	}

	for (int i = 0; i < n; i++) {
		int t_ev = json_array_nth(&doc, t_events, i);
		if (!json_token_is_object(&doc, t_ev)) {
			log_error("Timeline events[%d] must be an object", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		int t_kind = -1;
		int t_ev_name = -1;
		int t_oc = -1;
		int t_target = -1;
		if (!json_object_get(&doc, t_ev, "kind", &t_kind) || !json_object_get(&doc, t_ev, "name", &t_ev_name) || !json_object_get(&doc, t_ev, "on_complete", &t_oc)) {
			log_error("Timeline events[%d] missing required fields (kind, name, on_complete)", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		(void)json_object_get(&doc, t_ev, "target", &t_target);

		StringView sv_kind;
		StringView sv_ev_name;
		StringView sv_oc;
		if (!json_get_string(&doc, t_kind, &sv_kind) || sv_kind.len <= 0) {
			log_error("Timeline events[%d].kind must be a string", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		if (!json_get_string(&doc, t_ev_name, &sv_ev_name) || sv_ev_name.len <= 0) {
			log_error("Timeline events[%d].name must be a non-empty string", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		if (!json_get_string(&doc, t_oc, &sv_oc) || sv_oc.len <= 0) {
			log_error("Timeline events[%d].on_complete must be a string", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}

		char kind_buf[16];
		char oc_buf[16];
		if (sv_kind.len >= (int)sizeof(kind_buf) || sv_oc.len >= (int)sizeof(oc_buf)) {
			log_error("Timeline events[%d] kind/on_complete too long", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		memcpy(kind_buf, sv_kind.data, sv_kind.len);
		kind_buf[sv_kind.len] = '\0';
		memcpy(oc_buf, sv_oc.data, sv_oc.len);
		oc_buf[sv_oc.len] = '\0';

		TimelineEventKind kind;
		if (!parse_event_kind(kind_buf, &kind)) {
			log_error("Timeline events[%d].kind must be 'scene', 'map', or 'menu'", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}
		TimelineOnComplete on_complete;
		if (!parse_on_complete(oc_buf, &on_complete)) {
			log_error("Timeline events[%d].on_complete must be 'advance', 'loop', or 'load'", i);
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}

		TimelineEvent* ev = &out->events[i];
		ev->kind = kind;
		ev->on_complete = on_complete;
		ev->name = sv_dup(sv_ev_name);
		if (!ev->name) {
			timeline_destroy(out);
			json_doc_destroy(&doc);
			return false;
		}

		// Validate event.name based on kind.
		if (ev->kind == TIMELINE_EVENT_SCENE) {
			if (!name_is_safe_relpath(ev->name) || !ends_with_ci(ev->name, ".json")) {
				log_error("Timeline events[%d].name must be a safe relative .json path under Assets/Scenes: %s", i, ev->name);
				timeline_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		} else if (ev->kind == TIMELINE_EVENT_MAP) {
			if (!name_is_safe_filename(ev->name) || !ends_with_ci(ev->name, ".json")) {
				log_error("Timeline events[%d].name must be a safe .json filename under Assets/Levels (no slashes): %s", i, ev->name);
				timeline_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		} else if (ev->kind == TIMELINE_EVENT_MENU) {
			if (!name_is_safe_filename(ev->name) || !ends_with_ci(ev->name, ".json")) {
				log_error("Timeline events[%d].name must be a safe .json filename under Assets/Menus (no slashes): %s", i, ev->name);
				timeline_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		}

		if (on_complete == TIMELINE_ON_COMPLETE_LOAD) {
			if (t_target < 0) {
				log_error("Timeline events[%d] on_complete='load' requires 'target'", i);
				timeline_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			StringView sv_target;
			if (!json_get_string(&doc, t_target, &sv_target) || sv_target.len <= 0) {
				log_error("Timeline events[%d].target must be a non-empty string", i);
				timeline_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			ev->target = sv_dup(sv_target);
			if (!ev->target) {
				timeline_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
			if (!name_is_safe_relpath(ev->target) || !ends_with_ci(ev->target, ".json")) {
				log_error("Timeline events[%d].target must be a safe relative .json path under Assets/Timelines: %s", i, ev->target);
				timeline_destroy(out);
				json_doc_destroy(&doc);
				return false;
			}
		} else {
			// target must be absent or ignored. Ignore it if present.
			ev->target = NULL;
		}
	}

	log_info(
		"Timeline loaded: name='%s' events=%d pause_menu='%s'",
		out->name ? out->name : "(null)",
		out->event_count,
		(out->pause_menu && out->pause_menu[0] != '\0') ? out->pause_menu : "(none)"
	);
	json_doc_destroy(&doc);
	return true;
}
