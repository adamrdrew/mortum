#include "assets/map_validate.h"

#include "assets/map_loader.h"

#include "core/log.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include <string.h>

static MapValidationReport* g_report_sink = NULL;

static MapValidationContext mv_ctx_empty(void) {
	MapValidationContext c;
	c.sector_index = -1;
	c.wall_index = -1;
	c.vertex_index = -1;
	c.door_index = -1;
	c.entity_index = -1;
	c.light_index = -1;
	c.x = NAN;
	c.y = NAN;
	return c;
}

void map_validation_report_init(MapValidationReport* out) {
	if (!out) {
		return;
	}
	memset(out, 0, sizeof(*out));
}

void map_validation_report_destroy(MapValidationReport* report) {
	if (!report) {
		return;
	}
	for (int i = 0; i < report->error_count; i++) {
		free(report->errors[i].message);
		report->errors[i].message = NULL;
	}
	for (int i = 0; i < report->warning_count; i++) {
		free(report->warnings[i].message);
		report->warnings[i].message = NULL;
	}
	free(report->errors);
	free(report->warnings);
	memset(report, 0, sizeof(*report));
}

void map_validate_set_report_sink(MapValidationReport* report) {
	g_report_sink = report;
}

static void mv_error(const char* code, MapValidationContext ctx, const char* fmt, ...) {
	if (!code || !fmt) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (n < 0) {
		va_end(ap);
		return;
	}
	char* msg = (char*)malloc((size_t)n + 1);
	if (!msg) {
		va_end(ap);
		return;
	}
	(void)vsnprintf(msg, (size_t)n + 1, fmt, ap);
	va_end(ap);

	log_error("%s", msg);

	MapValidationReport* r = g_report_sink;
	if (!r) {
		free(msg);
		return;
	}
	if (r->error_count >= r->error_cap) {
		int next_cap = (r->error_cap == 0) ? 8 : (r->error_cap * 2);
		MapValidationEntry* next = (MapValidationEntry*)realloc(r->errors, (size_t)next_cap * sizeof(*next));
		if (!next) {
			free(msg);
			return;
		}
		r->errors = next;
		r->error_cap = next_cap;
	}
	MapValidationEntry e;
	e.code = code;
	e.message = msg;
	e.context = ctx;
	r->errors[r->error_count++] = e;
}

static void mv_warn(const char* code, MapValidationContext ctx, const char* fmt, ...) {
	if (!code || !fmt) {
		return;
	}
	va_list ap;
	va_start(ap, fmt);
	va_list ap2;
	va_copy(ap2, ap);
	int n = vsnprintf(NULL, 0, fmt, ap2);
	va_end(ap2);
	if (n < 0) {
		va_end(ap);
		return;
	}
	char* msg = (char*)malloc((size_t)n + 1);
	if (!msg) {
		va_end(ap);
		return;
	}
	(void)vsnprintf(msg, (size_t)n + 1, fmt, ap);
	va_end(ap);

	log_warn("%s", msg);

	MapValidationReport* r = g_report_sink;
	if (!r) {
		free(msg);
		return;
	}
	if (r->warning_count >= r->warning_cap) {
		int next_cap = (r->warning_cap == 0) ? 8 : (r->warning_cap * 2);
		MapValidationEntry* next = (MapValidationEntry*)realloc(r->warnings, (size_t)next_cap * sizeof(*next));
		if (!next) {
			free(msg);
			return;
		}
		r->warnings = next;
		r->warning_cap = next_cap;
	}
	MapValidationEntry e;
	e.code = code;
	e.message = msg;
	e.context = ctx;
	r->warnings[r->warning_count++] = e;
}

static float polygon_area2(const World* world, const int* loop_verts, int loop_len) {
	// Twice signed area (shoelace). loop_len >= 3.
	float a2 = 0.0f;
	for (int i = 0; i < loop_len; i++) {
		int ia = loop_verts[i];
		int ib = loop_verts[(i + 1) % loop_len];
		Vertex a = world->vertices[ia];
		Vertex b = world->vertices[ib];
		a2 += a.x * b.y - b.x * a.y;
	}
	return a2;
}

static bool point_in_loop_evenodd(const World* world, const int* loop_verts, int loop_len, float px, float py) {
	int crossings = 0;
	for (int i = 0; i < loop_len; i++) {
		Vertex a = world->vertices[loop_verts[i]];
		Vertex b = world->vertices[loop_verts[(i + 1) % loop_len]];
		if (fabsf(a.y - b.y) < 1e-8f) {
			continue;
		}
		bool cond = (a.y > py) != (b.y > py);
		if (!cond) {
			continue;
		}
		float x_int = (b.x - a.x) * (py - a.y) / (b.y - a.y) + a.x;
		if (px < x_int) {
			crossings ^= 1;
		}
	}
	return crossings != 0;
}

// Determine whether point lies in the sector's enclosing polygon.
// Uses the *largest closed loop* formed by the sector's walls (treated as undirected).
// This avoids internal wall segments (or obstacle loops) breaking the inside/outside test.
static bool sector_contains_point(const World* world, int sector, float px, float py) {
	if (!world || (unsigned)sector >= (unsigned)world->sector_count || world->vertex_count <= 0 || world->wall_count <= 0) {
		return false;
	}

	int vcount = world->vertex_count;
	int wcount = world->wall_count;

	int* deg = (int*)calloc((size_t)vcount, sizeof(int));
	bool* vused = (bool*)calloc((size_t)vcount, sizeof(bool));
	int* edge_a = (int*)malloc((size_t)wcount * sizeof(int));
	int* edge_b = (int*)malloc((size_t)wcount * sizeof(int));
	if (!deg || !vused || !edge_a || !edge_b) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		return false;
	}

	int ecount = 0;
	for (int i = 0; i < wcount; i++) {
		const Wall* w = &world->walls[i];
		if (w->front_sector != sector) {
			continue;
		}
		if (w->v0 < 0 || w->v0 >= vcount || w->v1 < 0 || w->v1 >= vcount || w->v0 == w->v1) {
			continue;
		}
		edge_a[ecount] = w->v0;
		edge_b[ecount] = w->v1;
		deg[w->v0]++;
		deg[w->v1]++;
		vused[w->v0] = true;
		vused[w->v1] = true;
		ecount++;
	}
	if (ecount < 3) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		return false;
	}

	// Build adjacency (neighbors list) for vertices used by sector edges.
	int* off = (int*)malloc(((size_t)vcount + 1) * sizeof(int));
	int* cur = (int*)calloc((size_t)vcount, sizeof(int));
	if (!off || !cur) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		free(off);
		free(cur);
		return false;
	}
	off[0] = 0;
	for (int v = 0; v < vcount; v++) {
		off[v + 1] = off[v] + deg[v];
	}
	int* nbr = (int*)malloc((size_t)off[vcount] * sizeof(int));
	if (!nbr) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		free(off);
		free(cur);
		return false;
	}
	for (int i = 0; i < ecount; i++) {
		int a = edge_a[i];
		int b = edge_b[i];
		nbr[off[a] + cur[a]++] = b;
		nbr[off[b] + cur[b]++] = a;
	}

	bool* vis = (bool*)calloc((size_t)vcount, sizeof(bool));
	int* q = (int*)malloc((size_t)vcount * sizeof(int));
	if (!vis || !q) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		free(off);
		free(cur);
		free(nbr);
		free(vis);
		free(q);
		return false;
	}

	float best_abs_area2 = -1.0f;
	int* best_loop = NULL;
	int best_loop_len = 0;

	for (int start = 0; start < vcount; start++) {
		if (!vused[start] || vis[start]) {
			continue;
		}
		// BFS component
		int qh = 0, qt = 0;
		q[qt++] = start;
		vis[start] = true;
		bool is_loop_component = true;
		int comp_vertex_count = 0;
		while (qh < qt) {
			int v = q[qh++];
			comp_vertex_count++;
			if (deg[v] != 2) {
				is_loop_component = false;
			}
			for (int k = off[v]; k < off[v + 1]; k++) {
				int n = nbr[k];
				if (!vis[n]) {
					vis[n] = true;
					q[qt++] = n;
				}
			}
		}
		if (!is_loop_component || comp_vertex_count < 3) {
			continue;
		}
		// Reconstruct ordered loop by walking neighbors.
		int* loop = (int*)malloc((size_t)comp_vertex_count * sizeof(int));
		if (!loop) {
			continue;
		}
		int prev = -1;
		int curv = start;
		for (int i = 0; i < comp_vertex_count; i++) {
			loop[i] = curv;
			int n0 = nbr[off[curv]];
			int n1 = nbr[off[curv] + 1];
			int nextv = (n0 != prev) ? n0 : n1;
			prev = curv;
			curv = nextv;
		}
		float a2 = polygon_area2(world, loop, comp_vertex_count);
		float abs_a2 = fabsf(a2);
		if (abs_a2 > best_abs_area2) {
			free(best_loop);
			best_loop = loop;
			best_loop_len = comp_vertex_count;
			best_abs_area2 = abs_a2;
		} else {
			free(loop);
		}
	}

	bool inside = false;
	if (best_loop && best_loop_len >= 3) {
		inside = point_in_loop_evenodd(world, best_loop, best_loop_len, px, py);
	}

	free(best_loop);
	free(deg);
	free(vused);
	free(edge_a);
	free(edge_b);
	free(off);
	free(cur);
	free(nbr);
	free(vis);
	free(q);
	return inside;
}

static int map_validate_find_sector_at_point(const World* world, float px, float py) {
	if (!world || world->sector_count <= 0) {
		return -1;
	}
	for (int s = 0; s < world->sector_count; s++) {
		if (sector_contains_point(world, s, px, py)) {
			return s;
		}
	}
	return -1;
}

static bool validate_sector_boundary(const World* world, int sector) {
	if (!world || (unsigned)sector >= (unsigned)world->sector_count) {
		return false;
	}
	if (world->vertex_count <= 0 || world->wall_count <= 0) {
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_error("MAP_MISSING_GEOMETRY", ctx, "Map must have vertices and walls");
		return false;
	}

	int vcount = world->vertex_count;
	int wcount = world->wall_count;

	int* deg = (int*)calloc((size_t)vcount, sizeof(int));
	bool* vused = (bool*)calloc((size_t)vcount, sizeof(bool));
	int* edge_a = (int*)malloc((size_t)wcount * sizeof(int));
	int* edge_b = (int*)malloc((size_t)wcount * sizeof(int));
	if (!deg || !vused || !edge_a || !edge_b) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_error("SECTOR_OOM", ctx, "Out of memory validating sector %d", sector);
		return false;
	}

	int ecount = 0;
	for (int i = 0; i < wcount; i++) {
		const Wall* w = &world->walls[i];
		if (w->front_sector != sector) {
			continue;
		}
		if (w->v0 < 0 || w->v0 >= vcount || w->v1 < 0 || w->v1 >= vcount) {
			continue;
		}
		if (w->v0 == w->v1) {
			free(deg);
			free(vused);
			free(edge_a);
			free(edge_b);
			MapValidationContext ctx = mv_ctx_empty();
			ctx.sector_index = sector;
			ctx.wall_index = i;
			mv_error("SECTOR_ZERO_LENGTH_WALL", ctx, "Sector %d has a zero-length wall edge (wall %d)", sector, i);
			return false;
		}
		edge_a[ecount] = w->v0;
		edge_b[ecount] = w->v1;
		deg[w->v0]++;
		deg[w->v1]++;
		vused[w->v0] = true;
		vused[w->v1] = true;
		ecount++;
	}
	if (ecount < 3) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_error("SECTOR_TOO_FEW_WALLS", ctx, "Sector %d has too few walls (%d). Is it missing walls?", sector, ecount);
		return false;
	}

	// Adjacency lists for undirected components.
	int* off = (int*)malloc(((size_t)vcount + 1) * sizeof(int));
	int* cur = (int*)calloc((size_t)vcount, sizeof(int));
	if (!off || !cur) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		free(off);
		free(cur);
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_error("SECTOR_OOM", ctx, "Out of memory validating sector %d", sector);
		return false;
	}
	off[0] = 0;
	for (int v = 0; v < vcount; v++) {
		off[v + 1] = off[v] + deg[v];
	}
	int* nbr = (int*)malloc((size_t)off[vcount] * sizeof(int));
	if (!nbr) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		free(off);
		free(cur);
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_error("SECTOR_OOM", ctx, "Out of memory validating sector %d", sector);
		return false;
	}
	for (int i = 0; i < ecount; i++) {
		int a = edge_a[i];
		int b = edge_b[i];
		nbr[off[a] + cur[a]++] = b;
		nbr[off[b] + cur[b]++] = a;
	}

	bool* vis = (bool*)calloc((size_t)vcount, sizeof(bool));
	int* q = (int*)malloc((size_t)vcount * sizeof(int));
	if (!vis || !q) {
		free(deg);
		free(vused);
		free(edge_a);
		free(edge_b);
		free(off);
		free(cur);
		free(nbr);
		free(vis);
		free(q);
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_error("SECTOR_OOM", ctx, "Out of memory validating sector %d", sector);
		return false;
	}

	int loops = 0;
	int open_components = 0;
	for (int start = 0; start < vcount; start++) {
		if (!vused[start] || vis[start]) {
			continue;
		}
		int qh = 0, qt = 0;
		q[qt++] = start;
		vis[start] = true;
		bool is_loop_component = true;
		int comp_v = 0;
		while (qh < qt) {
			int v = q[qh++];
			comp_v++;
			if (deg[v] != 2) {
				is_loop_component = false;
			}
			for (int k = off[v]; k < off[v + 1]; k++) {
				int n = nbr[k];
				if (!vis[n]) {
					vis[n] = true;
					q[qt++] = n;
				}
			}
		}
		if (is_loop_component && comp_v >= 3) {
			loops++;
		} else {
			open_components++;
		}
	}

	free(deg);
	free(vused);
	free(edge_a);
	free(edge_b);
	free(off);
	free(cur);
	free(nbr);
	free(vis);
	free(q);

	if (loops <= 0) {
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_error("SECTOR_NO_CLOSED_LOOP", ctx, "Sector %d has no closed boundary loop; it may leak to infinity", sector);
		return false;
	}
	if (open_components > 0) {
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_warn("SECTOR_OPEN_COMPONENTS", ctx, "Sector %d has %d wall components that are not closed loops (internal segments?)", sector, open_components);
	}
	if (loops > 1) {
		MapValidationContext ctx = mv_ctx_empty();
		ctx.sector_index = sector;
		mv_warn("SECTOR_MULTIPLE_LOOPS", ctx, "Sector %d has %d closed loops (obstacles/holes?)", sector, loops);
	}
	return true;
}

bool map_validate(const World* world, float player_start_x, float player_start_y, const MapDoor* doors, int door_count) {
	if (!world) {
		return false;
	}
	if (door_count < 0) {
		mv_error("MAP_INVALID_DOOR_COUNT", mv_ctx_empty(), "door_count < 0");
		return false;
	}
	if (door_count > 0 && !doors) {
		mv_error("MAP_DOORS_MISSING", mv_ctx_empty(), "doors missing (door_count=%d)", door_count);
		return false;
	}
	if (world->sector_count <= 0) {
		mv_error("MAP_NO_SECTORS", mv_ctx_empty(), "Map must have at least one sector");
		return false;
	}
	if (world->vertex_count <= 0) {
		mv_error("MAP_NO_VERTICES", mv_ctx_empty(), "Map must have at least one vertex");
		return false;
	}
	if (world->wall_count <= 0) {
		mv_error("MAP_NO_WALLS", mv_ctx_empty(), "Map must have at least one wall");
		return false;
	}
	for (int i = 0; i < world->sector_count; i++) {
		const Sector* s = &world->sectors[i];
		if (s->ceil_z <= s->floor_z) {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.sector_index = i;
			mv_error("SECTOR_INVALID_HEIGHTS", ctx, "Sector %d has ceil_z <= floor_z", i);
			return false;
		}
		if (s->floor_tex[0] == '\0') {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.sector_index = i;
			mv_error("SECTOR_MISSING_FLOOR_TEX", ctx, "Sector %d missing floor_tex", i);
			return false;
		}
		if (s->ceil_tex[0] == '\0') {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.sector_index = i;
			mv_error("SECTOR_MISSING_CEIL_TEX", ctx, "Sector %d missing ceil_tex", i);
			return false;
		}
		if (s->movable) {
			float max_floor = s->floor_z_origin;
			if (s->floor_z_toggled_pos > max_floor) {
				max_floor = s->floor_z_toggled_pos;
			}
			// Minimal clearance check: avoid impossible floor positions.
			if (s->ceil_z <= max_floor + 0.10f) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.sector_index = i;
				mv_error(
					"SECTOR_MOVABLE_NO_CLEARANCE",
					ctx,
					"Sector %d movable floor reaches/overlaps ceiling (ceil_z=%.3f max_floor=%.3f)",
					i,
					s->ceil_z,
					max_floor
				);
				return false;
			}
		}
		if (!validate_sector_boundary(world, i)) {
			return false;
		}
	}
	for (int i = 0; i < world->wall_count; i++) {
		Wall w = world->walls[i];
		if (w.v0 < 0 || w.v0 >= world->vertex_count || w.v1 < 0 || w.v1 >= world->vertex_count) {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.wall_index = i;
			mv_error("WALL_VERTEX_INDEX_OOR", ctx, "Wall %d vertex indices out of range", i);
			return false;
		}
		if ((unsigned)w.front_sector >= (unsigned)world->sector_count) {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.wall_index = i;
			mv_error("WALL_FRONT_SECTOR_OOR", ctx, "Wall %d front_sector out of range: %d", i, w.front_sector);
			return false;
		}
		if (w.back_sector != -1 && (unsigned)w.back_sector >= (unsigned)world->sector_count) {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.wall_index = i;
			mv_error("WALL_BACK_SECTOR_OOR", ctx, "Wall %d back_sector out of range: %d", i, w.back_sector);
			return false;
		}
		if (w.tex[0] == '\0') {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.wall_index = i;
			mv_error("WALL_MISSING_TEX", ctx, "Wall %d missing tex", i);
			return false;
		}
		if (w.end_level) {
			// end_level takes precedence over everything; disallow combinations that would be unreachable/ambiguous.
			if (w.toggle_sector) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.wall_index = i;
				mv_error(
					"WALL_END_LEVEL_AND_TOGGLE",
					ctx,
					"Wall %d has both end_level=true and toggle_sector=true (end_level takes precedence)",
					i
				);
				return false;
			}
			if (w.back_sector != -1) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.wall_index = i;
				mv_warn(
					"WALL_END_LEVEL_ON_PORTAL",
					ctx,
					"Wall %d has end_level=true on a portal wall (back_sector != -1); interaction may be possible from either side",
					i
				);
			}
		}
		if (w.toggle_sector) {
			if (w.toggle_sector_id != -1) {
				bool found = false;
				for (int s = 0; s < world->sector_count; s++) {
					if (world->sectors[s].id == w.toggle_sector_id) {
						found = true;
						break;
					}
				}
				if (!found) {
					MapValidationContext ctx = mv_ctx_empty();
					ctx.wall_index = i;
					mv_error("WALL_TOGGLE_SECTOR_ID_MISSING", ctx, "Wall %d toggle_sector_id refers to missing sector id: %d", i, w.toggle_sector_id);
					return false;
				}
			}
			// If active_tex is present, it must be non-empty.
			if (w.active_tex[0] == '\0') {
				// Allowed: empty means "no alternate texture".
			} else {
				// ok
			}
		}
	}

	// Doors: validate schema invariants that depend on world geometry.
	if (door_count > 0) {
		for (int i = 0; i < door_count; i++) {
			const MapDoor* d = &doors[i];
			if (!d->id[0]) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.door_index = i;
				mv_error("DOOR_MISSING_ID", ctx, "Door %d missing id", i);
				return false;
			}
			for (int j = 0; j < i; j++) {
				if (strcmp(doors[j].id, d->id) == 0) {
					MapValidationContext ctx = mv_ctx_empty();
					ctx.door_index = i;
					mv_error("DOOR_DUPLICATE_ID", ctx, "Door %d id duplicates prior id '%s'", i, d->id);
					return false;
				}
			}
			if (d->wall_index < 0 || d->wall_index >= world->wall_count) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.door_index = i;
				mv_error("DOOR_WALL_INDEX_OOR", ctx, "Door '%s' wall_index out of range: %d", d->id, d->wall_index);
				return false;
			}
			const Wall* w = &world->walls[d->wall_index];
			if (w->end_level) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.door_index = i;
				ctx.wall_index = d->wall_index;
				mv_error(
					"DOOR_BINDS_END_LEVEL_WALL",
					ctx,
					"Door '%s' wall_index=%d refers to a wall with end_level=true (end_level takes precedence)",
					d->id,
					d->wall_index
				);
				return false;
			}
			if (w->back_sector == -1) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.door_index = i;
				ctx.wall_index = d->wall_index;
				mv_error(
					"DOOR_NOT_PORTAL_WALL",
					ctx,
					"Door '%s' wall_index=%d must refer to a portal wall (back_sector != -1)",
					d->id,
					d->wall_index
				);
				return false;
			}
			if (d->tex[0] == '\0') {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.door_index = i;
				mv_error("DOOR_MISSING_TEX", ctx, "Door '%s' missing tex", d->id);
				return false;
			}
		}
	}

	// Optional: validate authored point lights (emitters).
	if (world->lights && world->light_count > 0) {
		for (int i = 0; i < world->light_count; i++) {
			if (world->light_alive && !world->light_alive[i]) {
				continue;
			}
			const PointLight* L = &world->lights[i];
			if (L->radius < 0.0f) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.light_index = i;
				ctx.x = L->x;
				ctx.y = L->y;
				mv_error("LIGHT_NEGATIVE_RADIUS", ctx, "light %d radius < 0", i);
				return false;
			}
			if (L->intensity < 0.0f) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.light_index = i;
				ctx.x = L->x;
				ctx.y = L->y;
				mv_error("LIGHT_NEGATIVE_INTENSITY", ctx, "light %d brightness/intensity < 0", i);
				return false;
			}
			// Authoring sanity: warn if light is outside the map.
			int s = world_find_sector_at_point(world, L->x, L->y);
			if (s < 0) {
				MapValidationContext ctx = mv_ctx_empty();
				ctx.light_index = i;
				ctx.x = L->x;
				ctx.y = L->y;
				mv_warn("LIGHT_OUTSIDE_SECTORS", ctx, "light %d at (%.3f, %.3f) is not inside any sector", i, L->x, L->y);
			}
		}
	}

	// Contiguity: all sectors reachable from player_start through portals.
	int start_sector = map_validate_find_sector_at_point(world, player_start_x, player_start_y);
	if (start_sector < 0) {
		MapValidationContext ctx = mv_ctx_empty();
		ctx.x = player_start_x;
		ctx.y = player_start_y;
		mv_error("PLAYER_START_OUTSIDE_SECTORS", ctx, "player_start is not inside any sector (x=%.3f y=%.3f)", player_start_x, player_start_y);
		return false;
	}

	int sc = world->sector_count;
	bool* adj = (bool*)calloc((size_t)sc * (size_t)sc, sizeof(bool));
	bool* vis = (bool*)calloc((size_t)sc, sizeof(bool));
	int* q = (int*)malloc((size_t)sc * sizeof(int));
	if (!adj || !vis || !q) {
		free(adj);
		free(vis);
		free(q);
		mv_error("OOM_SECTOR_ADJACENCY", mv_ctx_empty(), "Out of memory building sector adjacency");
		return false;
	}

	for (int i = 0; i < world->wall_count; i++) {
		const Wall* w = &world->walls[i];
		if (w->back_sector == -1) {
			continue;
		}
		int a = w->front_sector;
		int b = w->back_sector;
		if ((unsigned)a < (unsigned)sc && (unsigned)b < (unsigned)sc) {
			adj[a * sc + b] = true;
			adj[b * sc + a] = true;
		}
	}

	int qh = 0, qt = 0;
	q[qt++] = start_sector;
	vis[start_sector] = true;
	while (qh < qt) {
		int s = q[qh++];
		for (int n = 0; n < sc; n++) {
			if (adj[s * sc + n] && !vis[n]) {
				vis[n] = true;
				q[qt++] = n;
			}
		}
	}

	bool ok = true;
	for (int s = 0; s < sc; s++) {
		if (!vis[s]) {
			MapValidationContext ctx = mv_ctx_empty();
			ctx.sector_index = s;
			mv_error("SECTOR_NOT_REACHABLE", ctx, "Sector %d is not reachable from player_start sector %d via portals", s, start_sector);
			ok = false;
		}
	}

	free(adj);
	free(vis);
	free(q);
	return ok;
}
