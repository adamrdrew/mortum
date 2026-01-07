#include "game/gore.h"

#include "core/base.h"
#include "core/log.h"
#include "game/world.h"
#include "game/collision.h"
#include "render/camera.h"
#include "render/lighting.h"
#include "render/raycast.h"
#include "platform/time.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

// Simple xorshift32 for deterministic procedural splats.
static uint32_t xorshift32(uint32_t* state) {
        uint32_t x = *state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        *state = x;
        return x;
}

static uint32_t mix_seed(float x, float y, float z, uint32_t seed) {
        uint32_t xi = (uint32_t)llroundf(fabsf(x) * 73856093.0f);
        uint32_t yi = (uint32_t)llroundf(fabsf(y) * 19349663.0f);
        uint32_t zi = (uint32_t)llroundf(fabsf(z) * 83492791.0f);
        uint32_t h = xi ^ (yi << 1) ^ (zi << 2) ^ (seed ? seed : 0xA53B1Du);
        // Finalize with xorshift to spread bits.
        xorshift32(&h);
        return h ? h : 0xBAADF00Du;
}

static float randf(uint32_t* state) {
        return (float)(xorshift32(state) & 0xFFFFFFu) / (float)0x1000000u;
}

static inline float clampf3(float v, float lo, float hi) {
        if (v < lo) {
                return lo;
        }
        if (v > hi) {
                return hi;
        }
        return v;
}

// Stamps are floor-only in the current implementation, so we don't need arbitrary plane/tangent basis math.

bool gore_init(GoreSystem* self, int capacity) {
        if (!self) {
                return false;
        }
        memset(self, 0, sizeof(*self));
        if (capacity <= 0) {
                capacity = GORE_STAMP_MAX_DEFAULT;
        }
        self->items = (GoreStamp*)calloc((size_t)capacity, sizeof(GoreStamp));
        if (!self->items) {
                return false;
        }
        self->chunk_capacity = GORE_CHUNK_MAX_DEFAULT;
        self->chunks = (GoreChunk*)calloc((size_t)self->chunk_capacity, sizeof(GoreChunk));
        if (!self->chunks) {
                free(self->items);
                memset(self, 0, sizeof(*self));
                return false;
        }
        self->capacity = capacity;
        self->initialized = true;
        return true;
}

void gore_shutdown(GoreSystem* self) {
        if (!self) {
                return;
        }
        free(self->items);
        free(self->chunks);
        memset(self, 0, sizeof(*self));
}

void gore_reset(GoreSystem* self) {
        if (!self || !self->initialized) {
                return;
        }
        memset(self->items, 0, (size_t)self->capacity * sizeof(GoreStamp));
        if (self->chunks && self->chunk_capacity > 0) {
                memset(self->chunks, 0, (size_t)self->chunk_capacity * sizeof(GoreChunk));
        }
        self->alive_count = 0;
        self->chunk_alive = 0;
        self->stats_spawned = 0u;
        self->stats_dropped = 0u;
        self->stats_drawn_samples = 0u;
        self->stats_pixels_written = 0u;
}

void gore_begin_frame(GoreSystem* self) {
        if (!self || !self->initialized) {
                return;
        }
        self->stats_spawned = 0u;
        self->stats_dropped = 0u;
        self->stats_drawn_samples = 0u;
        self->stats_pixels_written = 0u;
}

static const float GORE_PALETTE[4][3] = {
        {1.0f, 1.0f, 1.0f},          // white
        {0.98f, 0.64f, 0.70f},       // pink
        {0.95f, 0.05f, 0.05f},       // bright red
        {0.35f, 0.04f, 0.06f},       // dark maroon
};

static void gore_snap_to_palette(float r, float g, float b, float* out_r, float* out_g, float* out_b) {
        float best = FLT_MAX;
        int best_idx = 0;
        for (int i = 0; i < 4; i++) {
                float dr = r - GORE_PALETTE[i][0];
                float dg = g - GORE_PALETTE[i][1];
                float db = b - GORE_PALETTE[i][2];
                float d2 = dr * dr + dg * dg + db * db;
                if (d2 < best) {
                        best = d2;
                        best_idx = i;
                }
        }
        if (out_r) {
                *out_r = GORE_PALETTE[best_idx][0];
        }
        if (out_g) {
                *out_g = GORE_PALETTE[best_idx][1];
        }
        if (out_b) {
                *out_b = GORE_PALETTE[best_idx][2];
        }
}

static inline float deg_to_rad3(float deg) {
        return deg * (float)M_PI / 180.0f;
}

static inline float max3(float a, float b) {
        return a > b ? a : b;
}

static bool gore_stamp_from_chunk(GoreSystem* self, const GoreChunk* c) {
        if (!self || !c) {
                return false;
        }
        GoreSpawnParams gp;
        memset(&gp, 0, sizeof(gp));
        gp.x = c->x;
        gp.y = c->y;
        gp.z = c->z;
        gp.radius = max3(c->radius * 2.25f, 0.05f);
        gp.sample_count = 4;
        gp.color_r = c->r;
        gp.color_g = c->g;
        gp.color_b = c->b;
        gp.life_ms = 0u;
        gp.seed = c->age_ms ^ (uint32_t)(fabsf(c->x * 100.0f));
        return gore_spawn(self, &gp);
}

static bool gore_chunk_collide_floorceil(
        GoreSystem* self,
        const World* world,
        GoreChunk* c,
        float new_z,
        float floor_z,
        float ceil_z) {
        if (new_z - c->radius <= floor_z) {
                c->z = floor_z;
                (void)world;
                gore_stamp_from_chunk(self, c);
                c->alive = false;
                return true;
        }
        if (new_z + c->radius >= ceil_z) {
                c->z = ceil_z;
                c->alive = false;
                return true;
        }
        return false;
}

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
        int last_valid_sector) {
        if (!self || !self->initialized || !self->chunks) {
                return false;
        }
        if (radius <= 0.0f) {
                return false;
        }
        int idx = -1;
        for (int i = 0; i < self->chunk_capacity; i++) {
                if (!self->chunks[i].alive) {
                        idx = i;
                        break;
                }
        }
        if (idx < 0) {
                self->stats_dropped++;
                return false;
        }
        GoreChunk c;
        memset(&c, 0, sizeof(c));
        c.alive = true;
        c.x = x;
        c.y = y;
        c.z = z;
        c.vx = vx;
        c.vy = vy;
        c.vz = vz;
        c.radius = radius;
        gore_snap_to_palette(clampf3(r, 0.0f, 1.0f), clampf3(g, 0.0f, 1.0f), clampf3(b, 0.0f, 1.0f), &c.r, &c.g, &c.b);
        c.life_ms = life_ms > 0u ? life_ms : 2800u;
        c.age_ms = 0u;
        c.last_valid_sector = last_valid_sector;
        c.sector = world ? world_find_sector_at_point_stable(world, x, y, last_valid_sector) : -1;
        self->chunks[idx] = c;
        self->chunk_alive++;
        return true;
}

void gore_tick(GoreSystem* self, const World* world, uint32_t dt_ms) {
        if (!self || !self->initialized || !self->items || dt_ms == 0u) {
                return;
        }
        int alive = 0;
        for (int i = 0; i < self->capacity; i++) {
                GoreStamp* g = &self->items[i];
                if (!g->alive) {
                        continue;
                }
                g->age_ms += dt_ms;
                if (g->life_ms > 0u && g->age_ms >= g->life_ms) {
                        g->alive = false;
                        continue;
                }
                alive++;
        }
        self->alive_count = alive;

        if (!self->chunks || !world) {
                return;
        }

        const float gravity = 18.0f;
        float dt = (float)dt_ms / 1000.0f;
        int chunk_alive = 0;
        for (int i = 0; i < self->chunk_capacity; i++) {
                GoreChunk* c = &self->chunks[i];
                if (!c->alive) {
                        continue;
                }
                c->age_ms += dt_ms;
                if (c->life_ms > 0u && c->age_ms >= c->life_ms) {
                        c->alive = false;
                        continue;
                }

                float to_x = c->x + c->vx * dt;
                float to_y = c->y + c->vy * dt;
                CollisionMoveResult mr = collision_move_circle(world, c->radius, c->x, c->y, to_x, to_y);
                bool hit_wall = mr.collided;

                c->x = mr.out_x;
                c->y = mr.out_y;
                c->vz -= gravity * dt;
                float new_z = c->z + c->vz * dt;

                int sec = world_find_sector_at_point_stable(world, c->x, c->y, c->last_valid_sector);
                c->sector = sec;
                if (sec >= 0 && (unsigned)sec < (unsigned)world->sector_count) {
                        c->last_valid_sector = sec;
                        float floor_z = world->sectors[sec].floor_z;
                        float ceil_z = world->sectors[sec].ceil_z;
                        if (gore_chunk_collide_floorceil(self, world, c, new_z, floor_z, ceil_z)) {
                                continue;
                        }
                }
                c->z = new_z;

                if (hit_wall) {
                        // Wall decals are intentionally disabled: in this renderer they tend to z-fight
                        // and look glitchy depending on view angle and distance.
                        c->alive = false;
                        continue;
                }
                chunk_alive++;
        }
        self->chunk_alive = chunk_alive;
}

bool gore_spawn(GoreSystem* self, const GoreSpawnParams* params) {
        if (!self || !self->initialized || !self->items || !params) {
                return false;
        }
        if (params->sample_count <= 0 || params->radius <= 0.0f) {
                return false;
        }
        // Find free slot.
        int idx = -1;
        for (int i = 0; i < self->capacity; i++) {
                if (!self->items[i].alive) {
                        idx = i;
                        break;
                }
        }
        if (idx < 0) {
                self->stats_dropped++;
                return false;
        }

        float pr = params->color_r;
        float pg = params->color_g;
        float pb = params->color_b;
        gore_snap_to_palette(pr, pg, pb, &pr, &pg, &pb);

        GoreStamp g;
        memset(&g, 0, sizeof(g));
        g.alive = true;
        g.x = params->x;
        g.y = params->y;
        g.z = params->z;
        g.life_ms = params->life_ms;
        g.max_radius = params->radius;

        int samples = params->sample_count;
        if (samples > GORE_STAMP_MAX_SAMPLES) {
                samples = GORE_STAMP_MAX_SAMPLES;
        }
        uint32_t rng = mix_seed(g.x, g.y, g.z, params->seed);
        for (int i = 0; i < samples; i++) {
                float angle = randf(&rng) * 2.0f * (float)M_PI;
                float radial = sqrtf(randf(&rng)) * params->radius;
                g.samples[i].off_x = radial * cosf(angle);
                g.samples[i].off_y = radial * sinf(angle);
                g.samples[i].radius = (0.35f + 0.65f * randf(&rng)) * (params->radius * 0.2f);
                g.samples[i].r = clampf3(pr, 0.0f, 1.0f);
                g.samples[i].g = clampf3(pg, 0.0f, 1.0f);
                g.samples[i].b = clampf3(pb, 0.0f, 1.0f);
                g.sample_count++;
        }

        self->items[idx] = g;
        self->alive_count++;
        self->stats_spawned++;
        return true;
}

static inline uint32_t pack_abgr_u8(uint8_t a, uint8_t b, uint8_t g, uint8_t r) {
        return ((uint32_t)a << 24u) | ((uint32_t)b << 16u) | ((uint32_t)g << 8u) | (uint32_t)r;
}

// Gore draws are opaque; no blending needed.

static float camera_world_z_for_sector_approx3(const World* world, int sector, float z_offset) {
        const float eye_height = 1.5f;
        const float headroom = 0.1f;
        if (!world || (unsigned)sector >= (unsigned)world->sector_count) {
                return eye_height + z_offset;
        }
        const Sector* s = &world->sectors[sector];
        float z = s->floor_z + eye_height + z_offset;
        float z_max = s->ceil_z - headroom;
        if (z > z_max) {
                z = z_max;
        }
        if (z < s->floor_z + headroom) {
                z = s->floor_z + headroom;
        }
        return z;
}

void gore_draw(
        GoreSystem* self,
        Framebuffer* fb,
        const World* world,
        const Camera* cam,
        int start_sector,
        const float* wall_depth,
        const float* depth_pixels) {
        if (!self || !self->initialized || !self->items || !fb || !fb->pixels || !world || !cam) {
                return;
        }
        if (!wall_depth && !depth_pixels) {
                return;
        }

        float cam_rad = deg_to_rad3(cam->angle_deg);
        float fx = cosf(cam_rad);
        float fy = sinf(cam_rad);
        float rx = -fy;
        float ry = fx;
        float fov_rad = deg_to_rad3(cam->fov_deg);
        float half_w = 0.5f * (float)fb->width;
        float half_h = 0.5f * (float)fb->height;
        float tan_half_fov = tanf(0.5f * fov_rad);
        if (tan_half_fov < 1e-4f) {
                return;
        }
        float focal = half_w / tan_half_fov;
        float cam_z_world = camera_world_z_for_sector_approx3(world, start_sector, cam->z);

        PointLight vis_lights[96];
        int vis_count = raycast_build_visible_lights(vis_lights, (int)MORTUM_ARRAY_COUNT(vis_lights), world, cam, (float)platform_time_seconds());

        uint32_t drawn_samples = 0u;
        uint32_t pixels_written = 0u;

        // Depth bias (world units along the ray depth axis) so stamps don't z-fight with the surface they sit on.
        // Stamps are biased slightly closer than the surface.
        const float stamp_depth_bias = 0.02f;
        const float chunk_depth_bias = 0.0f;

        // Draw live airborne chunks as opaque squares so their ballistic motion is visible.
        for (int ci = 0; ci < self->chunk_capacity; ci++) {
                const GoreChunk* c = &self->chunks[ci];
                if (!c->alive) {
                        continue;
                }
                float dx = c->x - cam->x;
                float dy = c->y - cam->y;
                float depth = dx * fx + dy * fy;
                if (depth <= 0.05f) {
                        continue;
                }
                float side = dx * rx + dy * ry;

                float proj_depth = depth;
                const float min_proj_depth = 0.25f;
                if (proj_depth < min_proj_depth) {
                        proj_depth = min_proj_depth;
                }
                float scale = focal / proj_depth;
                int radius_px = (int)(c->radius * scale + 0.5f);
                if (radius_px < 1) {
                        radius_px = 1;
                }
                int max_dim = fb->width > fb->height ? fb->width : fb->height;
                if (radius_px > max_dim) {
                        radius_px = max_dim;
                }

                float x_center = half_w + side * scale;
                float y_center = half_h + (cam_z_world - c->z) * scale;

                int x0 = (int)(x_center - (float)radius_px);
                int x1 = (int)(x_center + (float)radius_px + 1);
                int y0 = (int)(y_center - (float)radius_px);
                int y1 = (int)(y_center + (float)radius_px + 1);

                int clip_x0 = x0 < 0 ? 0 : x0;
                int clip_x1 = x1 > fb->width ? fb->width : x1;
                int clip_y0 = y0 < 0 ? 0 : y0;
                int clip_y1 = y1 > fb->height ? fb->height : y1;
                if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) {
                        continue;
                }

                int sec = world_find_sector_at_point_stable(world, c->x, c->y, start_sector);
                float sector_intensity = 1.0f;
                LightColor sector_tint = light_color_white();
                if ((unsigned)sec < (unsigned)world->sector_count) {
                        sector_intensity = world->sectors[sec].light;
                        sector_tint = world->sectors[sec].light_color;
                }

                float dist = sqrtf(dx * dx + dy * dy);
                uint8_t a = 255u;
                uint8_t r = (uint8_t)lroundf(clampf3(c->r, 0.0f, 1.0f) * 255.0f);
                uint8_t gch = (uint8_t)lroundf(clampf3(c->g, 0.0f, 1.0f) * 255.0f);
                uint8_t b = (uint8_t)lroundf(clampf3(c->b, 0.0f, 1.0f) * 255.0f);
                uint32_t src_px = pack_abgr_u8(a, b, gch, r);
                src_px = lighting_apply(src_px, dist, sector_intensity, sector_tint, vis_lights, vis_count, c->x, c->y);

                bool any = false;
                for (int x = clip_x0; x < clip_x1; x++) {
                        if (wall_depth && depth >= (wall_depth[x] + chunk_depth_bias)) {
                                continue;
                        }
                        for (int y = clip_y0; y < clip_y1; y++) {
                                if (depth_pixels) {
                                        float world_depth = depth_pixels[y * fb->width + x];
                                        if (depth >= (world_depth + chunk_depth_bias)) {
                                                continue;
                                        }
                                }
                                fb->pixels[y * fb->width + x] = src_px;
                                pixels_written++;
                                any = true;
                        }
                }
                if (any) {
                        drawn_samples++;
                }
        }

        for (int i = 0; i < self->capacity; i++) {
                const GoreStamp* g = &self->items[i];
                if (!g->alive || g->sample_count <= 0) {
                        continue;
                }
                for (int si = 0; si < g->sample_count; si++) {
                        const GoreSample* s = &g->samples[si];
                        float wx = g->x + s->off_x;
                        float wy = g->y + s->off_y;
                        float wz = g->z;

                        float dx = wx - cam->x;
                        float dy = wy - cam->y;
                        float depth = dx * fx + dy * fy;
                        if (depth <= 0.05f) {
                                continue;
                        }
                        float side = dx * rx + dy * ry;

                        float proj_depth = depth;
                        const float min_proj_depth = 0.25f;
                        if (proj_depth < min_proj_depth) {
                                proj_depth = min_proj_depth;
                        }
                        float scale = focal / proj_depth;
                        int radius_px = (int)(s->radius * scale + 0.5f);
                        if (radius_px < 1) {
                                radius_px = 1;
                        }
                        int max_dim = fb->width > fb->height ? fb->width : fb->height;
                        if (radius_px > max_dim) {
                                radius_px = max_dim;
                        }

                        float x_center = half_w + side * scale;
                        float y_center = half_h + (cam_z_world - wz) * scale;

                        int x0 = (int)(x_center - (float)radius_px);
                        int x1 = (int)(x_center + (float)radius_px + 1);
                        int y0 = (int)(y_center - (float)radius_px);
                        int y1 = (int)(y_center + (float)radius_px + 1);

                        int clip_x0 = x0 < 0 ? 0 : x0;
                        int clip_x1 = x1 > fb->width ? fb->width : x1;
                        int clip_y0 = y0 < 0 ? 0 : y0;
                        int clip_y1 = y1 > fb->height ? fb->height : y1;
                        if (clip_x0 >= clip_x1 || clip_y0 >= clip_y1) {
                                continue;
                        }

                        int sec = world_find_sector_at_point_stable(world, wx, wy, start_sector);
                        float sector_intensity = 1.0f;
                        LightColor sector_tint = light_color_white();
                        if ((unsigned)sec < (unsigned)world->sector_count) {
                                sector_intensity = world->sectors[sec].light;
                                sector_tint = world->sectors[sec].light_color;
                        }

                        float dist = sqrtf(dx * dx + dy * dy);
                        uint8_t a = 255u;
                        uint8_t r = (uint8_t)lroundf(clampf3(s->r, 0.0f, 1.0f) * 255.0f);
                        uint8_t gch = (uint8_t)lroundf(clampf3(s->g, 0.0f, 1.0f) * 255.0f);
                        uint8_t b = (uint8_t)lroundf(clampf3(s->b, 0.0f, 1.0f) * 255.0f);
                        uint32_t src_px = pack_abgr_u8(a, b, gch, r);
                        src_px = lighting_apply(src_px, dist, sector_intensity, sector_tint, vis_lights, vis_count, wx, wy);

                        bool any = false;
                        for (int x = clip_x0; x < clip_x1; x++) {
                                if (wall_depth && depth >= (wall_depth[x] + stamp_depth_bias)) {
                                        continue;
                                }
                                for (int y = clip_y0; y < clip_y1; y++) {
                                        if (depth_pixels) {
                                                float world_depth = depth_pixels[y * fb->width + x];
                                                if (depth >= (world_depth + stamp_depth_bias)) {
                                                        continue;
                                                }
                                        }
                                        fb->pixels[y * fb->width + x] = src_px;
                                        pixels_written++;
                                        any = true;
                                }
                        }
                        if (any) {
                                drawn_samples++;
                        }
                }
        }
        self->stats_drawn_samples = drawn_samples;
        self->stats_pixels_written = pixels_written;
}
