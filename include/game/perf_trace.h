#pragma once

#include <stdbool.h>
#include <stdio.h>

// Simple in-engine performance trace for manual profiling.
// Usage: call perf_trace_start(), then each frame call perf_trace_record_frame().
// After PERF_TRACE_FRAME_COUNT frames, a compact report is printed and the trace stops.

#define PERF_TRACE_FRAME_COUNT 60

typedef struct PerfTraceFrame {
	double frame_ms;
	double update_ms;
	double render3d_ms;
	double ui_ms;
	double present_ms;
	int steps;
} PerfTraceFrame;

typedef struct PerfTrace {
	bool active;
	int count;
	PerfTraceFrame frames[PERF_TRACE_FRAME_COUNT];
	char map_name[64];
	int fb_w;
	int fb_h;
} PerfTrace;

void perf_trace_init(PerfTrace* t);

// Starts (or restarts) a trace capture.
void perf_trace_start(PerfTrace* t, const char* map_name, int fb_w, int fb_h);

bool perf_trace_is_active(const PerfTrace* t);

// Records one frame. When the trace reaches PERF_TRACE_FRAME_COUNT frames, it will
// print the summary to `out` and automatically stop.
void perf_trace_record_frame(PerfTrace* t, const PerfTraceFrame* frame, FILE* out);
