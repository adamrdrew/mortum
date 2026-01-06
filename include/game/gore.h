#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "render/framebuffer.h"

// Purpose-built gore/blood system (separate from particles) for persistent, "sticky" splats.
// Gore stamps are planar, procedurally generated blobs that cling to walls/floors/ceilings and
// are pooled separately from the particle emitter pipeline.

#define GORE_STAMP_MAX_DEFAULT 512
#define GORE_CHUNK_MAX_DEFAULT 768
#define GORE_STAMP_MAX_SAMPLES 24

typedef struct World World;
typedef struct Camera Camera;
typedef struct TextureRegistry TextureRegistry;
typedef struct AssetPaths AssetPaths;

typedef struct GoreSample {
        float off_r;      // offset along tangent/right basis (world units)
        float off_u;      // offset along bitangent/up basis (world units)
        float radius;     // world-unit radius of this droplet
        float r;
        float g;
        float b;
} GoreSample;

typedef struct GoreStamp {
        bool alive;
        uint32_t age_ms;
        uint32_t life_ms; // 0 => persistent

        float x;
        float y;
        float z;

        float n_x;
        float n_y;
        float n_z;

        // Orthonormal tangent basis spanning the gore plane (right, up).
        float r_x;
        float r_y;
        float r_z;
        float u_x;
        float u_y;
        float u_z;

        float max_radius;

        int sample_count;
        GoreSample samples[GORE_STAMP_MAX_SAMPLES];
} GoreStamp;

typedef struct GoreChunk {
        bool alive;
        float x;
        float y;
        float z;
        float vx;
        float vy;
        float vz;
        float radius;
        float r;
        float g;
        float b;
        uint32_t age_ms;
        uint32_t life_ms;
        int sector;
        int last_valid_sector;
} GoreChunk;

typedef struct GoreSystem {
        bool initialized;
        int capacity;
        GoreStamp* items; // owned
        int alive_count;

        int chunk_capacity;
        GoreChunk* chunks; // owned
        int chunk_alive;

        // Per-frame stats (cleared by gore_begin_frame).
        uint32_t stats_spawned;
        uint32_t stats_dropped;
        uint32_t stats_drawn_samples;
        uint32_t stats_pixels_written;
} GoreSystem;

typedef struct GoreSpawnParams {
        float x;
        float y;
        float z;

        // Preferred surface normal (normalized internally; defaults to +Z when invalid).
        float n_x;
        float n_y;
        float n_z;

        float radius;        // world units, overall footprint radius
        int sample_count;    // number of procedural droplets to generate
        float color_r;       // [0..1]
        float color_g;       // [0..1]
        float color_b;       // [0..1]
        float anisotropy;    // 0..1 stretch droplets along tangent
        uint32_t life_ms;    // 0 => persistent
        uint32_t seed;       // deterministic seed; 0 derives from position
} GoreSpawnParams;

bool gore_init(GoreSystem* self, int capacity);
void gore_shutdown(GoreSystem* self);
void gore_reset(GoreSystem* self);

void gore_begin_frame(GoreSystem* self);
void gore_tick(GoreSystem* self, const World* world, uint32_t dt_ms);

// Spawns a procedural gore stamp. Drops newest when pool is full.
bool gore_spawn(GoreSystem* self, const GoreSpawnParams* params);

// Spawn a flying gore chunk with ballistic motion; drops newest when chunk pool is full.
bool gore_spawn_chunk(
        GoreSystem* self,
        const World* world,
        float x,
        float y,
        float z,
        float vx,
        float vy,
        float vz,
        float radius,
        float r,
        float g,
        float b,
        uint32_t life_ms,
        int last_valid_sector);

// Draws all alive gore stamps using procedural droplets; respects depth/wall occlusion.
void gore_draw(
        GoreSystem* self,
        Framebuffer* fb,
        const World* world,
        const Camera* cam,
        int start_sector,
        const float* wall_depth,
        const float* depth_pixels);

