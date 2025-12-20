#pragma once

#include <stdbool.h>

typedef struct GameLoop {
	double fixed_dt_s;
	double accumulator_s;
	double last_time_s;
	int max_steps_per_frame;
} GameLoop;

void game_loop_init(GameLoop* self, double fixed_dt_s);

// Returns number of fixed updates to run this frame.
int game_loop_begin_frame(GameLoop* self, double now_s);

// Returns interpolation alpha in [0,1].
double game_loop_alpha(const GameLoop* self);
