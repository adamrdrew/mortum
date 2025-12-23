#include "game/perf_trace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int cmp_double_asc(const void* a, const void* b) {
	double da = *(const double*)a;
	double db = *(const double*)b;
	return (da > db) - (da < db);
}

typedef struct PerfStats {
	double min;
	double max;
	double avg;
	double p50;
	double p95;
} PerfStats;

static double percentile_sorted(const double* sorted, int n, double p) {
	if (n <= 0) {
		return 0.0;
	}
	if (p <= 0.0) {
		return sorted[0];
	}
	if (p >= 1.0) {
		return sorted[n - 1];
	}
	double pos = p * (double)(n - 1);
	int i0 = (int)floor(pos);
	int i1 = i0 + 1;
	if (i1 >= n) {
		return sorted[n - 1];
	}
	double t = pos - (double)i0;
	return sorted[i0] * (1.0 - t) + sorted[i1] * t;
}

static PerfStats compute_stats(const double* values, int n) {
	PerfStats s = { 0 };
	if (n <= 0) {
		return s;
	}
	s.min = values[0];
	s.max = values[0];
	double sum = 0.0;
	for (int i = 0; i < n; i++) {
		double v = values[i];
		if (v < s.min) {
			s.min = v;
		}
		if (v > s.max) {
			s.max = v;
		}
		sum += v;
	}
	s.avg = sum / (double)n;

	double tmp[PERF_TRACE_FRAME_COUNT];
	int m = n;
	if (m > PERF_TRACE_FRAME_COUNT) {
		m = PERF_TRACE_FRAME_COUNT;
	}
	for (int i = 0; i < m; i++) {
		tmp[i] = values[i];
	}
	qsort(tmp, (size_t)m, sizeof(tmp[0]), cmp_double_asc);
	s.p50 = percentile_sorted(tmp, m, 0.50);
	s.p95 = percentile_sorted(tmp, m, 0.95);
	return s;
}

static void print_stats_line(FILE* out, const char* label, const PerfStats* s) {
	fprintf(out, "%-10s avg=%6.2f  p95=%6.2f  min=%6.2f  max=%6.2f\n", label, s->avg, s->p95, s->min, s->max);
}

static void print_stats_line_ms_precise(FILE* out, const char* label, const PerfStats* s) {
	// Useful for tiny sub-millisecond timings that would otherwise round to 0.00.
	fprintf(out, "%-10s avg=%7.4f  p95=%7.4f  min=%7.4f  max=%7.4f\n", label, s->avg, s->p95, s->min, s->max);
}

void perf_trace_init(PerfTrace* t) {
	if (!t) {
		return;
	}
	memset(t, 0, sizeof(*t));
}

void perf_trace_start(PerfTrace* t, const char* map_name, int fb_w, int fb_h) {
	if (!t) {
		return;
	}
	t->active = true;
	t->count = 0;
	t->fb_w = fb_w;
	t->fb_h = fb_h;
	memset(t->frames, 0, sizeof(t->frames));
	t->map_name[0] = '\0';
	if (map_name) {
		strncpy(t->map_name, map_name, sizeof(t->map_name) - 1);
		t->map_name[sizeof(t->map_name) - 1] = '\0';
	}
}

bool perf_trace_is_active(const PerfTrace* t) {
	return t && t->active;
}

static void perf_trace_dump(const PerfTrace* t, FILE* out) {
	if (!t) {
		return;
	}
	if (!out) {
		out = stdout;
	}
	int n = t->count;
	if (n <= 0) {
		return;
	}

	double frame_ms[PERF_TRACE_FRAME_COUNT];
	double update_ms[PERF_TRACE_FRAME_COUNT];
	double render3d_ms[PERF_TRACE_FRAME_COUNT];
	double ui_ms[PERF_TRACE_FRAME_COUNT];
	double present_ms[PERF_TRACE_FRAME_COUNT];
	double steps_d[PERF_TRACE_FRAME_COUNT];

	double rc_planes_ms[PERF_TRACE_FRAME_COUNT];
	double rc_hit_ms[PERF_TRACE_FRAME_COUNT];
	double rc_walls_ms[PERF_TRACE_FRAME_COUNT];
	double rc_tex_lookup_ms[PERF_TRACE_FRAME_COUNT];
	double rc_light_cull_ms[PERF_TRACE_FRAME_COUNT];
	double rc_tex_get_calls[PERF_TRACE_FRAME_COUNT];
	double rc_registry_compares[PERF_TRACE_FRAME_COUNT];
	double rc_portal_calls[PERF_TRACE_FRAME_COUNT];
	double rc_portal_depth[PERF_TRACE_FRAME_COUNT];
	double rc_wall_tests[PERF_TRACE_FRAME_COUNT];
	double rc_pix_floor[PERF_TRACE_FRAME_COUNT];
	double rc_pix_ceil[PERF_TRACE_FRAME_COUNT];
	double rc_pix_wall[PERF_TRACE_FRAME_COUNT];
	double rc_lights_world[PERF_TRACE_FRAME_COUNT];
	double rc_lights_visible_uncapped[PERF_TRACE_FRAME_COUNT];
	double rc_lights_visible_walls[PERF_TRACE_FRAME_COUNT];
	double rc_lights_visible_planes[PERF_TRACE_FRAME_COUNT];
	double rc_lighting_apply_calls[PERF_TRACE_FRAME_COUNT];
	double rc_lighting_mul_calls[PERF_TRACE_FRAME_COUNT];
	double rc_lighting_apply_iters[PERF_TRACE_FRAME_COUNT];
	double rc_lighting_mul_iters[PERF_TRACE_FRAME_COUNT];
	double rc_lighting_total_iters[PERF_TRACE_FRAME_COUNT];

	int worst_i = 0;
	double worst_frame = t->frames[0].frame_ms;
	int max_steps = t->frames[0].steps;

	for (int i = 0; i < n; i++) {
		const PerfTraceFrame* f = &t->frames[i];
		frame_ms[i] = f->frame_ms;
		update_ms[i] = f->update_ms;
		render3d_ms[i] = f->render3d_ms;
		ui_ms[i] = f->ui_ms;
		present_ms[i] = f->present_ms;
		steps_d[i] = (double)f->steps;
		rc_planes_ms[i] = f->rc_planes_ms;
		rc_hit_ms[i] = f->rc_hit_test_ms;
		rc_walls_ms[i] = f->rc_walls_ms;
		rc_tex_lookup_ms[i] = f->rc_tex_lookup_ms;
		rc_light_cull_ms[i] = f->rc_light_cull_ms;
		rc_tex_get_calls[i] = (double)f->rc_texture_get_calls;
		rc_registry_compares[i] = (double)f->rc_registry_compares;
		rc_portal_calls[i] = (double)f->rc_portal_calls;
		rc_portal_depth[i] = (double)f->rc_portal_max_depth;
		rc_wall_tests[i] = (double)f->rc_wall_ray_tests;
		rc_pix_floor[i] = (double)f->rc_pixels_floor;
		rc_pix_ceil[i] = (double)f->rc_pixels_ceil;
		rc_pix_wall[i] = (double)f->rc_pixels_wall;
		rc_lights_world[i] = (double)f->rc_lights_in_world;
		rc_lights_visible_uncapped[i] = (double)f->rc_lights_visible_uncapped;
		rc_lights_visible_walls[i] = (double)f->rc_lights_visible_walls;
		rc_lights_visible_planes[i] = (double)f->rc_lights_visible_planes;
		rc_lighting_apply_calls[i] = (double)f->rc_lighting_apply_calls;
		rc_lighting_mul_calls[i] = (double)f->rc_lighting_mul_calls;
		rc_lighting_apply_iters[i] = (double)f->rc_lighting_apply_light_iters;
		rc_lighting_mul_iters[i] = (double)f->rc_lighting_mul_light_iters;
		rc_lighting_total_iters[i] = rc_lighting_apply_iters[i] + rc_lighting_mul_iters[i];
		if (f->steps > max_steps) {
			max_steps = f->steps;
		}
		if (f->frame_ms > worst_frame) {
			worst_frame = f->frame_ms;
			worst_i = i;
		}
	}

	PerfStats s_frame = compute_stats(frame_ms, n);
	PerfStats s_update = compute_stats(update_ms, n);
	PerfStats s_r3d = compute_stats(render3d_ms, n);
	PerfStats s_ui = compute_stats(ui_ms, n);
	PerfStats s_present = compute_stats(present_ms, n);
	PerfStats s_steps = compute_stats(steps_d, n);
	PerfStats s_rc_planes = compute_stats(rc_planes_ms, n);
	PerfStats s_rc_hit = compute_stats(rc_hit_ms, n);
	PerfStats s_rc_walls = compute_stats(rc_walls_ms, n);
	PerfStats s_rc_tex = compute_stats(rc_tex_lookup_ms, n);
	PerfStats s_rc_lcull = compute_stats(rc_light_cull_ms, n);
	PerfStats s_rc_tex_get = compute_stats(rc_tex_get_calls, n);
	PerfStats s_rc_cmp = compute_stats(rc_registry_compares, n);
	PerfStats s_rc_portals = compute_stats(rc_portal_calls, n);
	PerfStats s_rc_depth = compute_stats(rc_portal_depth, n);
	PerfStats s_rc_tests = compute_stats(rc_wall_tests, n);
	PerfStats s_rc_pf = compute_stats(rc_pix_floor, n);
	PerfStats s_rc_pc = compute_stats(rc_pix_ceil, n);
	PerfStats s_rc_pw = compute_stats(rc_pix_wall, n);
	PerfStats s_rc_lw = compute_stats(rc_lights_world, n);
	PerfStats s_rc_lvu = compute_stats(rc_lights_visible_uncapped, n);
	PerfStats s_rc_lvw = compute_stats(rc_lights_visible_walls, n);
	PerfStats s_rc_lvp = compute_stats(rc_lights_visible_planes, n);
	PerfStats s_rc_lac = compute_stats(rc_lighting_apply_calls, n);
	PerfStats s_rc_lmc = compute_stats(rc_lighting_mul_calls, n);
	PerfStats s_rc_lai = compute_stats(rc_lighting_apply_iters, n);
	PerfStats s_rc_lmi = compute_stats(rc_lighting_mul_iters, n);
	PerfStats s_rc_lti = compute_stats(rc_lighting_total_iters, n);

	int max_visible_walls = (int)t->frames[0].rc_lights_visible_walls;
	int max_visible_planes = (int)t->frames[0].rc_lights_visible_planes;
	int max_visible_lights_uncapped = (int)t->frames[0].rc_lights_visible_uncapped;

	double avg_fps = (s_frame.avg > 1e-9) ? (1000.0 / s_frame.avg) : 0.0;
	const PerfTraceFrame* w = &t->frames[worst_i];
	for (int i = 0; i < n; i++) {
		if (t->frames[i].rc_lights_visible_walls > max_visible_walls) {
			max_visible_walls = t->frames[i].rc_lights_visible_walls;
		}
		if (t->frames[i].rc_lights_visible_planes > max_visible_planes) {
			max_visible_planes = t->frames[i].rc_lights_visible_planes;
		}
		if (t->frames[i].rc_lights_visible_uncapped > max_visible_lights_uncapped) {
			max_visible_lights_uncapped = t->frames[i].rc_lights_visible_uncapped;
		}
	}

	fprintf(out, "\n=== MORTUM PERF TRACE (%d frames) ===\n", n);
	fprintf(out, "map: %s\n", t->map_name[0] ? t->map_name : "(unknown)");
	if (t->fb_w > 0 && t->fb_h > 0) {
		fprintf(out, "resolution: %dx%d\n", t->fb_w, t->fb_h);
	}
	fprintf(out, "avg_fps: %.1f\n", avg_fps);
	print_stats_line(out, "frame_ms", &s_frame);
	print_stats_line(out, "update_ms", &s_update);
	print_stats_line(out, "render3d", &s_r3d);
	fprintf(out, "render3d_breakdown (includes sampling+lighting):\n");
	print_stats_line(out, "  planes", &s_rc_planes);
	print_stats_line(out, "  hit", &s_rc_hit);
	print_stats_line(out, "  walls", &s_rc_walls);
	print_stats_line(out, "  texget", &s_rc_tex);
	print_stats_line_ms_precise(out, "  lcull", &s_rc_lcull);
	fprintf(out, "lighting (point lights):\n");
	fprintf(out, "  lights avg: world=%.1f  planes=%.1f  walls=%.1f  uncapped=%.1f  max_planes=%d  max_walls=%d (uncapped=%d)\n", s_rc_lw.avg, s_rc_lvp.avg, s_rc_lvw.avg, s_rc_lvu.avg, max_visible_planes, max_visible_walls, max_visible_lights_uncapped);
	fprintf(out, "  calls avg: apply=%.0f  mul=%.0f\n", s_rc_lac.avg, s_rc_lmc.avg);
	fprintf(out, "  iters avg: apply=%.0f  mul=%.0f  total=%.0f\n", s_rc_lai.avg, s_rc_lmi.avg, s_rc_lti.avg);
	print_stats_line(out, "ui_ms", &s_ui);
	print_stats_line(out, "present", &s_present);
	fprintf(out, "steps      avg=%6.2f  p95=%6.2f  min=%6.0f  max=%6d\n", s_steps.avg, s_steps.p95, s_steps.min, max_steps);
	fprintf(out,
		"renderer_counts avg: portals=%.1f depth=%.1f  tex_get=%.0f  strcmps=%.0f  wall_tests=%.0f\n",
		s_rc_portals.avg,
		s_rc_depth.avg,
		s_rc_tex_get.avg,
		s_rc_cmp.avg,
		s_rc_tests.avg);
	fprintf(out,
		"pixels_written avg: floor=%.0f  ceil=%.0f  wall=%.0f\n",
		s_rc_pf.avg,
		s_rc_pc.avg,
		s_rc_pw.avg);
	fprintf(out,
		"worst_frame i=%d  frame_ms=%.2f  render3d=%.2f (planes=%.2f hit=%.2f walls=%.2f texget=%.2f)\n",
		worst_i,
		w->frame_ms,
		w->render3d_ms,
		w->rc_planes_ms,
		w->rc_hit_test_ms,
		w->rc_walls_ms,
		w->rc_tex_lookup_ms);
	fprintf(out,
		"worst_frame_lighting: lights world=%d planes=%d walls=%d uncapped=%d  lcull=%.2fms  apply_calls=%d  apply_iters=%d\n",
		w->rc_lights_in_world,
		w->rc_lights_visible_planes,
		w->rc_lights_visible_walls,
		w->rc_lights_visible_uncapped,
		w->rc_light_cull_ms,
		w->rc_lighting_apply_calls,
		w->rc_lighting_apply_light_iters);
	fprintf(out, "=== END PERF TRACE ===\n");
	fflush(out);
}

void perf_trace_record_frame(PerfTrace* t, const PerfTraceFrame* frame, FILE* out) {
	if (!t || !frame || !t->active) {
		return;
	}
	if (t->count < 0 || t->count >= PERF_TRACE_FRAME_COUNT) {
		// Defensive: reset on out-of-range.
		t->count = 0;
	}
	t->frames[t->count] = *frame;
	t->count++;
	if (t->count >= PERF_TRACE_FRAME_COUNT) {
		perf_trace_dump(t, out);
		t->active = false;
	}
}
