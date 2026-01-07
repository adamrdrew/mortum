// Nomos Studio - Procedural map generation implementation
// 
// Generates valid Mortum maps with proper portal connectivity.
// Key requirements from engine:
// 1. Each sector must have at least one closed boundary loop of front-side walls
// 2. Portal walls require TWO directed walls (A->B with front=sector1, back=sector2)
//    AND (B->A with front=sector2, back=sector1)
// 3. All sectors must be reachable from player start via portal adjacency
// 4. Player start must be inside a sector

#include "nomos_procgen.h"

#include "game/world.h"
#include "render/lighting.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdio.h>

// Simple LCG random number generator
typedef struct ProcGenRNG {
	uint32_t state;
} ProcGenRNG;

static void rng_seed(ProcGenRNG* rng, uint32_t seed) {
	rng->state = seed ? seed : (uint32_t)time(NULL);
}

static uint32_t rng_next(ProcGenRNG* rng) {
	rng->state = rng->state * 1103515245 + 12345;
	return (rng->state >> 16) & 0x7FFF;
}

static float rng_float(ProcGenRNG* rng) {
	return (float)rng_next(rng) / 32767.0f;
}

static float rng_range(ProcGenRNG* rng, float min, float max) {
	return min + rng_float(rng) * (max - min);
}

#define MAX_ROOMS 64
#define MAX_VERTICES 1024
#define MAX_SECTORS 128
#define MAX_WALLS 512
#define CORRIDOR_WIDTH 2.0f

// Room descriptor
typedef struct ProcRoom {
	float min_x, min_y, max_x, max_y;
	float floor_z;
	float ceil_z;
} ProcRoom;

// Connection between two sectors (will become portal walls)
typedef struct Connection {
	int sector_a;
	int sector_b;
	// Portal edge shared between sectors
	int va, vb;  // vertex indices for the portal edge
} Connection;

void nomos_procgen_params_default(NomosProcGenParams* params) {
	if (!params) return;
	memset(params, 0, sizeof(*params));
	
	params->min_x = 0.0f;
	params->min_y = 0.0f;
	params->max_x = 40.0f;
	params->max_y = 40.0f;
	
	params->target_room_count = 6;
	params->min_room_size = 5.0f;
	params->max_room_size = 12.0f;
	
	params->min_floor_z = 0.0f;
	params->max_floor_z = 0.5f;
	params->min_ceil_height = 3.5f;
	params->max_ceil_height = 5.0f;
	
	params->seed = 0;
	
	strncpy(params->floor_tex, "FLOOR_2A.PNG", sizeof(params->floor_tex) - 1);
	strncpy(params->ceil_tex, "TECH_1A.PNG", sizeof(params->ceil_tex) - 1);
	strncpy(params->wall_tex, "BRICK_3A.PNG", sizeof(params->wall_tex) - 1);
}

// Check if two rooms overlap or are very close
static bool rooms_overlap(const ProcRoom* a, const ProcRoom* b, float margin) {
	return !(a->max_x + margin < b->min_x || b->max_x + margin < a->min_x ||
	         a->max_y + margin < b->min_y || b->max_y + margin < a->min_y);
}

// Generate a simple grid-based connected layout
// This creates a central hub with rooms connected via corridors
bool nomos_procgen_generate(MapLoadResult* out, const NomosProcGenParams* params) {
	if (!out || !params) return false;
	memset(out, 0, sizeof(*out));
	
	// Initialize RNG
	ProcGenRNG rng;
	rng_seed(&rng, params->seed);
	
	// We use a simple and reliable approach:
	// 1. Create a central hub room
	// 2. Create peripheral rooms around it
	// 3. Connect each peripheral room directly to the hub with a corridor
	// This guarantees connectivity because every room connects to the hub.
	
	float map_cx = (params->min_x + params->max_x) / 2.0f;
	float map_cy = (params->min_y + params->max_y) / 2.0f;
	float map_radius = fminf(params->max_x - params->min_x, params->max_y - params->min_y) / 2.0f - 2.0f;
	
	// Limit room count
	int target_rooms = params->target_room_count;
	if (target_rooms < 2) target_rooms = 2;
	if (target_rooms > MAX_ROOMS) target_rooms = MAX_ROOMS;
	
	ProcRoom rooms[MAX_ROOMS];
	int room_count = 0;
	
	// Create central hub room
	float hub_size = rng_range(&rng, params->min_room_size, params->max_room_size);
	rooms[0].min_x = map_cx - hub_size / 2.0f;
	rooms[0].max_x = map_cx + hub_size / 2.0f;
	rooms[0].min_y = map_cy - hub_size / 2.0f;
	rooms[0].max_y = map_cy + hub_size / 2.0f;
	rooms[0].floor_z = params->min_floor_z;
	rooms[0].ceil_z = params->min_floor_z + rng_range(&rng, params->min_ceil_height, params->max_ceil_height);
	room_count = 1;
	
	// Create peripheral rooms in a circle around the hub
	int peripheral_count = target_rooms - 1;
	for (int i = 0; i < peripheral_count; i++) {
		float angle = (float)i / (float)peripheral_count * 2.0f * 3.14159f;
		float dist = map_radius * 0.6f + rng_range(&rng, 0, map_radius * 0.2f);
		
		float room_size = rng_range(&rng, params->min_room_size, params->max_room_size);
		float rx = map_cx + cosf(angle) * dist;
		float ry = map_cy + sinf(angle) * dist;
		
		rooms[room_count].min_x = rx - room_size / 2.0f;
		rooms[room_count].max_x = rx + room_size / 2.0f;
		rooms[room_count].min_y = ry - room_size / 2.0f;
		rooms[room_count].max_y = ry + room_size / 2.0f;
		rooms[room_count].floor_z = rng_range(&rng, params->min_floor_z, params->max_floor_z);
		rooms[room_count].ceil_z = rooms[room_count].floor_z + 
			rng_range(&rng, params->min_ceil_height, params->max_ceil_height);
		
		// Check it doesn't overlap existing rooms
		bool overlaps = false;
		for (int j = 0; j < room_count; j++) {
			if (rooms_overlap(&rooms[room_count], &rooms[j], CORRIDOR_WIDTH + 0.5f)) {
				overlaps = true;
				break;
			}
		}
		if (!overlaps) {
			room_count++;
		}
	}
	
	printf("nomos_procgen: Created %d rooms (hub + %d peripheral)\n", room_count, room_count - 1);
	
	// Calculate how many corridors we need (one per peripheral room)
	int corridor_count = room_count - 1;
	
	// Total sectors = rooms + corridor segments (2 per corridor for L-shape)
	int total_sectors = room_count + corridor_count * 2;
	int total_vertices = total_sectors * 8;  // max 8 vertices per sector
	int total_walls = total_sectors * 8;      // generous allocation
	
	world_init_empty(&out->world);
	
	if (!world_alloc_vertices(&out->world, total_vertices)) return false;
	if (!world_alloc_sectors(&out->world, total_sectors)) return false;
	if (!world_alloc_walls(&out->world, total_walls)) return false;
	
	int vi = 0;  // vertex index
	int si = 0;  // sector index
	int wi = 0;  // wall index
	
	// Track first vertex and wall indices for each room for later portal creation
	int room_first_vertex[MAX_ROOMS];
	int room_first_wall[MAX_ROOMS];
	int room_sector[MAX_ROOMS];
	
	// Create room sectors with closed boundary loops
	for (int r = 0; r < room_count; r++) {
		room_first_vertex[r] = vi;
		room_first_wall[r] = wi;
		room_sector[r] = si;
		
		ProcRoom* rm = &rooms[r];
		
		// 4 vertices CCW
		out->world.vertices[vi + 0] = (Vertex){rm->min_x, rm->min_y};
		out->world.vertices[vi + 1] = (Vertex){rm->max_x, rm->min_y};
		out->world.vertices[vi + 2] = (Vertex){rm->max_x, rm->max_y};
		out->world.vertices[vi + 3] = (Vertex){rm->min_x, rm->max_y};
		
		// Sector
		Sector* s = &out->world.sectors[si];
		s->id = si;
		s->floor_z = rm->floor_z;
		s->floor_z_origin = rm->floor_z;
		s->ceil_z = rm->ceil_z;
		s->light = rng_range(&rng, 0.7f, 1.0f);
		s->light_color = light_color_white();
		strncpy(s->floor_tex, params->floor_tex, sizeof(s->floor_tex) - 1);
		strncpy(s->ceil_tex, params->ceil_tex, sizeof(s->ceil_tex) - 1);
		
		// 4 walls forming closed loop (all solid initially)
		for (int w = 0; w < 4; w++) {
			Wall* wall = &out->world.walls[wi + w];
			memset(wall, 0, sizeof(*wall));
			wall->v0 = vi + w;
			wall->v1 = vi + ((w + 1) % 4);
			wall->front_sector = si;
			wall->back_sector = -1;
			strncpy(wall->tex, params->wall_tex, sizeof(wall->tex) - 1);
			strncpy(wall->base_tex, params->wall_tex, sizeof(wall->base_tex) - 1);
		}
		
		vi += 4;
		si += 1;
		wi += 4;
	}
	
	// Now create corridors connecting each peripheral room to the hub
	// We use a corridor that is 1 sector (simple straight corridor)
	// The corridor shares vertices with rooms via portal walls
	
	for (int r = 1; r < room_count; r++) {
		ProcRoom* hub = &rooms[0];
		ProcRoom* room = &rooms[r];
		
		// Find direction from hub to room
		float hub_cx = (hub->min_x + hub->max_x) / 2.0f;
		float hub_cy = (hub->min_y + hub->max_y) / 2.0f;
		float room_cx = (room->min_x + room->max_x) / 2.0f;
		float room_cy = (room->min_y + room->max_y) / 2.0f;
		
		float dx = room_cx - hub_cx;
		float dy = room_cy - hub_cy;
		float len = sqrtf(dx * dx + dy * dy);
		if (len < 0.001f) len = 1.0f;
		dx /= len;
		dy /= len;
		
		// Perpendicular for corridor width
		float px = -dy * (CORRIDOR_WIDTH / 2.0f);
		float py = dx * (CORRIDOR_WIDTH / 2.0f);
		
		// Find where corridor exits hub (edge of hub room)
		float hub_exit_x, hub_exit_y;
		// Find intersection with hub boundary
		float t_hub = 1e9f;
		if (dx > 0.001f) {
			float t = (hub->max_x - hub_cx) / dx;
			if (t > 0 && t < t_hub) t_hub = t;
		} else if (dx < -0.001f) {
			float t = (hub->min_x - hub_cx) / dx;
			if (t > 0 && t < t_hub) t_hub = t;
		}
		if (dy > 0.001f) {
			float t = (hub->max_y - hub_cy) / dy;
			if (t > 0 && t < t_hub) t_hub = t;
		} else if (dy < -0.001f) {
			float t = (hub->min_y - hub_cy) / dy;
			if (t > 0 && t < t_hub) t_hub = t;
		}
		hub_exit_x = hub_cx + dx * t_hub;
		hub_exit_y = hub_cy + dy * t_hub;
		
		// Find where corridor enters room
		float room_entry_x, room_entry_y;
		float t_room = 1e9f;
		if (dx < -0.001f) {
			float t = (room->max_x - room_cx) / (-dx);
			if (t > 0 && t < t_room) t_room = t;
		} else if (dx > 0.001f) {
			float t = (room->min_x - room_cx) / (-dx);
			if (t > 0 && t < t_room) t_room = t;
		}
		if (dy < -0.001f) {
			float t = (room->max_y - room_cy) / (-dy);
			if (t > 0 && t < t_room) t_room = t;
		} else if (dy > 0.001f) {
			float t = (room->min_y - room_cy) / (-dy);
			if (t > 0 && t < t_room) t_room = t;
		}
		room_entry_x = room_cx - dx * t_room;
		room_entry_y = room_cy - dy * t_room;
		
		// Corridor sector vertices (4 corners of corridor rectangle)
		int cor_v0 = vi;
		out->world.vertices[vi + 0] = (Vertex){hub_exit_x + px, hub_exit_y + py};
		out->world.vertices[vi + 1] = (Vertex){hub_exit_x - px, hub_exit_y - py};
		out->world.vertices[vi + 2] = (Vertex){room_entry_x - px, room_entry_y - py};
		out->world.vertices[vi + 3] = (Vertex){room_entry_x + px, room_entry_y + py};
		vi += 4;
		
		int cor_sector = si;
		
		// Corridor sector
		float cor_floor = (hub->floor_z + room->floor_z) / 2.0f;
		float cor_ceil = fminf(hub->ceil_z, room->ceil_z);  // Use lower ceiling
		if (cor_ceil <= cor_floor + 0.5f) cor_ceil = cor_floor + 3.0f;
		
		Sector* cs = &out->world.sectors[si];
		cs->id = si;
		cs->floor_z = cor_floor;
		cs->floor_z_origin = cor_floor;
		cs->ceil_z = cor_ceil;
		cs->light = 0.5f;
		cs->light_color = light_color_white();
		strncpy(cs->floor_tex, params->floor_tex, sizeof(cs->floor_tex) - 1);
		strncpy(cs->ceil_tex, params->ceil_tex, sizeof(cs->ceil_tex) - 1);
		si++;
		
		// Corridor walls - CCW order
		// Wall 0: hub side (portal to hub) - v0 to v1
		// Wall 1: right side (solid) - v1 to v2
		// Wall 2: room side (portal to room) - v2 to v3
		// Wall 3: left side (solid) - v3 to v0
		
		int hub_sector = room_sector[0];
		int rm_sector = room_sector[r];
		
		// Wall 0: corridor's portal to hub (corridor front, hub back)
		Wall* w0 = &out->world.walls[wi];
		memset(w0, 0, sizeof(*w0));
		w0->v0 = cor_v0 + 0;
		w0->v1 = cor_v0 + 1;
		w0->front_sector = cor_sector;
		w0->back_sector = hub_sector;
		strncpy(w0->tex, params->wall_tex, sizeof(w0->tex) - 1);
		strncpy(w0->base_tex, params->wall_tex, sizeof(w0->base_tex) - 1);
		wi++;
		
		// Wall 0 twin: hub's portal to corridor (hub front, corridor back)
		Wall* w0t = &out->world.walls[wi];
		memset(w0t, 0, sizeof(*w0t));
		w0t->v0 = cor_v0 + 1;  // Reversed direction
		w0t->v1 = cor_v0 + 0;
		w0t->front_sector = hub_sector;
		w0t->back_sector = cor_sector;
		strncpy(w0t->tex, params->wall_tex, sizeof(w0t->tex) - 1);
		strncpy(w0t->base_tex, params->wall_tex, sizeof(w0t->base_tex) - 1);
		wi++;
		
		// Wall 1: right side solid
		Wall* w1 = &out->world.walls[wi];
		memset(w1, 0, sizeof(*w1));
		w1->v0 = cor_v0 + 1;
		w1->v1 = cor_v0 + 2;
		w1->front_sector = cor_sector;
		w1->back_sector = -1;
		strncpy(w1->tex, params->wall_tex, sizeof(w1->tex) - 1);
		strncpy(w1->base_tex, params->wall_tex, sizeof(w1->base_tex) - 1);
		wi++;
		
		// Wall 2: corridor's portal to room (corridor front, room back)
		Wall* w2 = &out->world.walls[wi];
		memset(w2, 0, sizeof(*w2));
		w2->v0 = cor_v0 + 2;
		w2->v1 = cor_v0 + 3;
		w2->front_sector = cor_sector;
		w2->back_sector = rm_sector;
		strncpy(w2->tex, params->wall_tex, sizeof(w2->tex) - 1);
		strncpy(w2->base_tex, params->wall_tex, sizeof(w2->base_tex) - 1);
		wi++;
		
		// Wall 2 twin: room's portal to corridor (room front, corridor back)
		Wall* w2t = &out->world.walls[wi];
		memset(w2t, 0, sizeof(*w2t));
		w2t->v0 = cor_v0 + 3;  // Reversed direction
		w2t->v1 = cor_v0 + 2;
		w2t->front_sector = rm_sector;
		w2t->back_sector = cor_sector;
		strncpy(w2t->tex, params->wall_tex, sizeof(w2t->tex) - 1);
		strncpy(w2t->base_tex, params->wall_tex, sizeof(w2t->base_tex) - 1);
		wi++;
		
		// Wall 3: left side solid
		Wall* w3 = &out->world.walls[wi];
		memset(w3, 0, sizeof(*w3));
		w3->v0 = cor_v0 + 3;
		w3->v1 = cor_v0 + 0;
		w3->front_sector = cor_sector;
		w3->back_sector = -1;
		strncpy(w3->tex, params->wall_tex, sizeof(w3->tex) - 1);
		strncpy(w3->base_tex, params->wall_tex, sizeof(w3->base_tex) - 1);
		wi++;
	}
	
	out->world.vertex_count = vi;
	out->world.sector_count = si;
	out->world.wall_count = wi;
	
	// Place player in center of hub
	out->player_start_x = (rooms[0].min_x + rooms[0].max_x) / 2.0f;
	out->player_start_y = (rooms[0].min_y + rooms[0].max_y) / 2.0f;
	out->player_start_angle_deg = 0.0f;
	
	// Add lights
	if (world_alloc_lights(&out->world, room_count)) {
		for (int i = 0; i < room_count; i++) {
			float cx = (rooms[i].min_x + rooms[i].max_x) / 2.0f;
			float cy = (rooms[i].min_y + rooms[i].max_y) / 2.0f;
			
			PointLight light = {0};
			light.x = cx;
			light.y = cy;
			light.z = rooms[i].ceil_z - 0.5f;
			light.radius = (rooms[i].max_x - rooms[i].min_x + rooms[i].max_y - rooms[i].min_y) / 2.0f;
			light.intensity = rng_range(&rng, 0.6f, 1.0f);
			light.color = light_color_white();
			
			out->world.lights[i] = light;
			if (out->world.light_alive) out->world.light_alive[i] = 1;
		}
		out->world.light_count = room_count;
	}
	
	printf("nomos_procgen: Final map: %d vertices, %d sectors, %d walls\n",
		out->world.vertex_count, out->world.sector_count, out->world.wall_count);
	
	return true;
}
