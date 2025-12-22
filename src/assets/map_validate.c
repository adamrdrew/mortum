#include "assets/map_validate.h"

#include "core/log.h"

#include <math.h>
#include <stdlib.h>

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
		log_error("Map must have vertices and walls");
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
		log_error("Out of memory validating sector %d", sector);
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
			log_error("Sector %d has a zero-length wall edge (wall %d)", sector, i);
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
		log_error("Sector %d has too few walls (%d). Is it missing walls?", sector, ecount);
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
		log_error("Out of memory validating sector %d", sector);
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
		log_error("Out of memory validating sector %d", sector);
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
		log_error("Out of memory validating sector %d", sector);
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
		log_error("Sector %d has no closed boundary loop; it may leak to infinity", sector);
		return false;
	}
	if (open_components > 0) {
		log_warn("Sector %d has %d wall components that are not closed loops (internal segments?)", sector, open_components);
	}
	if (loops > 1) {
		log_warn("Sector %d has %d closed loops (obstacles/holes?)", sector, loops);
	}
	return true;
}

bool map_validate(const World* world, float player_start_x, float player_start_y) {
	if (!world) {
		return false;
	}
	if (world->sector_count <= 0) {
		log_error("Map must have at least one sector");
		return false;
	}
	if (world->vertex_count <= 0) {
		log_error("Map must have at least one vertex");
		return false;
	}
	if (world->wall_count <= 0) {
		log_error("Map must have at least one wall");
		return false;
	}
	for (int i = 0; i < world->sector_count; i++) {
		if (world->sectors[i].ceil_z <= world->sectors[i].floor_z) {
			log_error("Sector %d has ceil_z <= floor_z", i);
			return false;
		}
		if (world->sectors[i].floor_tex[0] == '\0') {
			log_error("Sector %d missing floor_tex", i);
			return false;
		}
		if (world->sectors[i].ceil_tex[0] == '\0') {
			log_error("Sector %d missing ceil_tex", i);
			return false;
		}
		if (!validate_sector_boundary(world, i)) {
			return false;
		}
	}
	for (int i = 0; i < world->wall_count; i++) {
		Wall w = world->walls[i];
		if (w.v0 < 0 || w.v0 >= world->vertex_count || w.v1 < 0 || w.v1 >= world->vertex_count) {
			log_error("Wall %d vertex indices out of range", i);
			return false;
		}
		if ((unsigned)w.front_sector >= (unsigned)world->sector_count) {
			log_error("Wall %d front_sector out of range: %d", i, w.front_sector);
			return false;
		}
		if (w.back_sector != -1 && (unsigned)w.back_sector >= (unsigned)world->sector_count) {
			log_error("Wall %d back_sector out of range: %d", i, w.back_sector);
			return false;
		}
		if (w.tex[0] == '\0') {
			log_error("Wall %d missing tex", i);
			return false;
		}
	}

	// Contiguity: all sectors reachable from player_start through portals.
	int start_sector = map_validate_find_sector_at_point(world, player_start_x, player_start_y);
	if (start_sector < 0) {
		log_error("player_start is not inside any sector (x=%.3f y=%.3f)", player_start_x, player_start_y);
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
		log_error("Out of memory building sector adjacency");
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
			log_error("Sector %d is not reachable from player_start sector %d via portals", s, start_sector);
			ok = false;
		}
	}

	free(adj);
	free(vis);
	free(q);
	return ok;
}
