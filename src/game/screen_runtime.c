#include "game/screen_runtime.h"

#include <stddef.h>

static void screen_destroy(Screen* s) {
	if (s && s->v && s->v->destroy) {
		s->v->destroy(s);
	}
}

void screen_runtime_init(ScreenRuntime* self) {
	if (!self) {
		return;
	}
	self->active = NULL;
}

void screen_runtime_shutdown(ScreenRuntime* self, const ScreenContext* ctx) {
	if (!self) {
		return;
	}
	if (self->active && self->active->v && self->active->v->on_exit) {
		self->active->v->on_exit(self->active, ctx);
	}
	screen_destroy(self->active);
	self->active = NULL;
}

bool screen_runtime_is_active(const ScreenRuntime* self) {
	return self && self->active != NULL;
}

void screen_runtime_set(ScreenRuntime* self, Screen* screen, const ScreenContext* ctx) {
	if (!self) {
		screen_destroy(screen);
		return;
	}
	if (self->active && self->active->v && self->active->v->on_exit) {
		self->active->v->on_exit(self->active, ctx);
	}
	screen_destroy(self->active);
	self->active = screen;
	if (self->active && self->active->v && self->active->v->on_enter) {
		self->active->v->on_enter(self->active, ctx);
	}
}

bool screen_runtime_update(ScreenRuntime* self, const ScreenContext* ctx, double dt_s) {
	if (!self || !self->active || !self->active->v || !self->active->v->update) {
		return false;
	}
	ScreenResult r = self->active->v->update(self->active, ctx, dt_s);
	if (r == SCREEN_RESULT_DONE) {
		if (self->active->v->on_exit) {
			self->active->v->on_exit(self->active, ctx);
		}
		screen_destroy(self->active);
		self->active = NULL;
		return true;
	}
	return false;
}

void screen_runtime_draw(ScreenRuntime* self, const ScreenContext* ctx) {
	if (!self || !self->active || !self->active->v || !self->active->v->draw) {
		return;
	}
	self->active->v->draw(self->active, ctx);
}
