#include "core/game_loop.h"

static double clamp01(double v) {
	if (v < 0.0) {
		return 0.0;
	}
	if (v > 1.0) {
		return 1.0;
	}
	return v;
}

void game_loop_init(GameLoop* self, double fixed_dt_s) {
	self->fixed_dt_s = fixed_dt_s;
	self->accumulator_s = 0.0;
	self->last_time_s = 0.0;
	self->max_steps_per_frame = 8;
}

int game_loop_begin_frame(GameLoop* self, double now_s) {
	if (self->last_time_s == 0.0) {
		self->last_time_s = now_s;
		return 0;
	}

	double frame_dt = now_s - self->last_time_s;
	if (frame_dt < 0.0) {
		frame_dt = 0.0;
	}
	if (frame_dt > 0.25) {
		frame_dt = 0.25;
	}

	self->last_time_s = now_s;
	self->accumulator_s += frame_dt;

	int steps = 0;
	while (self->accumulator_s >= self->fixed_dt_s && steps < self->max_steps_per_frame) {
		self->accumulator_s -= self->fixed_dt_s;
		steps++;
	}

	return steps;
}

double game_loop_alpha(const GameLoop* self) {
	if (self->fixed_dt_s <= 0.0) {
		return 0.0;
	}
	return clamp01(self->accumulator_s / self->fixed_dt_s);
}
