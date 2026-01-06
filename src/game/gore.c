#include "game/gore.h"

#include "core/base.h"
#include "core/log.h"
#include "game/world.h"
#include "render/camera.h"

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

static bool normalize3(float* x, float* y, float* z) {
        float len = sqrtf((*x) * (*x) + (*y) * (*y) + (*z) * (*z));
        if (len < 1e-6f) {
                return false;
        }
        *x /= len;
        *y /= len;
        *z /= len;
        return true;
}

static void make_basis_from_normal(float nx, float ny, float nz, float* rx, float* ry, float* rz, float* ux, float* uy, float* uz) {
        float n_x = nx;
        float n_y = ny;
        float n_z = nz;
        if (!normalize3(&n_x, &n_y, &n_z)) {
                n_x = 0.0f;
                n_y = 0.0f;
                n_z = 1.0f;
        }
        // Choose an arbitrary vector not parallel to n for cross product.
        float ax = fabsf(n_x) > 0.9f ? 0.0f : 1.0f;
        float ay = 0.0f;
        float az = fabsf(n_x) > 0.9f ? 1.0f : 0.0f;

        // r = normalize(cross(n, a))
        float r_x = n_y * az - n_z * ay;
        float r_y = n_z * ax - n_x * az;
        float r_z = n_x * ay - n_y * ax;
        if (!normalize3(&r_x, &r_y, &r_z)) {
                r_x = 1.0f;
                r_y = 0.0f;
                r_z = 0.0f;
        }
        // u = cross(r, n)
        float u_x = r_y * n_z - r_z * n_y;
        float u_y = r_z * n_x - r_x * n_z;
        float u_z = r_x * n_y - r_y * n_x;
        (void)normalize3(&u_x, &u_y, &u_z);

        *rx = r_x;
        *ry = r_y;
        *rz = r_z;
        *ux = u_x;
        *uy = u_y;
        *uz = u_z;
}

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
        self->capacity = capacity;
        self->initialized = true;
        return true;
}

void gore_shutdown(GoreSystem* self) {
        if (!self) {
                return;
        }
        free(self->items);
        memset(self, 0, sizeof(*self));
}

void gore_reset(GoreSystem* self) {
        if (!self || !self->initialized) {
                return;
        }
        memset(self->items, 0, (size_t)self->capacity * sizeof(GoreStamp));
        self->alive_count = 0;
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

void gore_tick(GoreSystem* self, uint32_t dt_ms) {
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
}

static float clamp_and_mix(float base, float spread, uint32_t* rng) {
        float delta = (randf(rng) * 2.0f - 1.0f) * spread;
        return clampf3(base + delta, 0.0f, 1.0f);
}

bool gore_spawn(GoreSystem* self, const GoreSpawnParams* params) {
        if (!self || !self->initialized || !self->items || !params) {
                return false;
        }
        if (params->sample_count <= 0 || params->radius <= 0.0f || params->opacity <= 0.0f) {
                return false;
        }
        if (self->alive_count >= self->capacity) {
            self->stats_dropped++;
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

        GoreStamp g;
        memset(&g, 0, sizeof(g));
        g.alive = true;
        g.x = params->x;
        g.y = params->y;
        g.z = params->z;
        g.n_x = params->n_x;
        g.n_y = params->n_y;
        g.n_z = params->n_z;
        g.life_ms = params->life_ms;
        g.max_radius = params->radius;

        make_basis_from_normal(g.n_x, g.n_y, g.n_z, &g.r_x, &g.r_y, &g.r_z, &g.u_x, &g.u_y, &g.u_z);

        int samples = params->sample_count;
        if (samples > GORE_STAMP_MAX_SAMPLES) {
                samples = GORE_STAMP_MAX_SAMPLES;
        }
        uint32_t rng = mix_seed(g.x, g.y, g.z, params->seed);
        for (int i = 0; i < samples; i++) {
                float angle = randf(&rng) * 2.0f * (float)M_PI;
                float radial = sqrtf(randf(&rng)) * params->radius;
                float stretch = 1.0f + params->anisotropy * (randf(&rng) * 0.75f);
                float rr = radial * stretch;
                g.samples[i].off_r = rr * cosf(angle);
                g.samples[i].off_u = radial * sinf(angle);
                g.samples[i].radius = (0.1f + 0.9f * randf(&rng)) * (params->radius * 0.15f);
                g.samples[i].opacity = clampf3(params->opacity * (0.6f + 0.4f * randf(&rng)), 0.0f, 1.0f);
                g.samples[i].r = clamp_and_mix(params->color_r, params->color_spread, &rng);
                g.samples[i].g = clamp_and_mix(params->color_g, params->color_spread, &rng);
                g.samples[i].b = clamp_and_mix(params->color_b, params->color_spread, &rng);
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

static inline uint32_t blend_abgr8888_over(uint32_t src, uint32_t dst) {
        uint32_t sa = (src >> 24u) & 0xFFu;
        if (sa == 0u) {
                return dst;
        }
        if (sa == 255u) {
                return src;
        }
        uint32_t inv = 255u - sa;
        uint32_t sb = (src >> 16u) & 0xFFu;
        uint32_t sg = (src >> 8u) & 0xFFu;
        uint32_t sr = src & 0xFFu;
        uint32_t da = (dst >> 24u) & 0xFFu;
        uint32_t db = (dst >> 16u) & 0xFFu;
        uint32_t dg = (dst >> 8u) & 0xFFu;
        uint32_t dr = dst & 0xFFu;
        uint32_t oa = sa + (da * inv + 127u) / 255u;
        uint32_t ob = (sb * sa + db * inv + 127u) / 255u;
        uint32_t og = (sg * sa + dg * inv + 127u) / 255u;
        uint32_t or_ = (sr * sa + dr * inv + 127u) / 255u;
        return (oa << 24u) | (ob << 16u) | (og << 8u) | or_;
}

static inline float deg_to_rad3(float deg) {
        return deg * (float)M_PI / 180.0f;
}

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

        uint32_t drawn_samples = 0u;
        uint32_t pixels_written = 0u;

        for (int i = 0; i < self->capacity; i++) {
                const GoreStamp* g = &self->items[i];
                if (!g->alive || g->sample_count <= 0) {
                        continue;
                }
                for (int si = 0; si < g->sample_count; si++) {
                        const GoreSample* s = &g->samples[si];
                        float wx = g->x + g->r_x * s->off_r + g->u_x * s->off_u;
                        float wy = g->y + g->r_y * s->off_r + g->u_y * s->off_u;
                        float wz = g->z + g->r_z * s->off_r + g->u_z * s->off_u;

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

                        uint8_t a = (uint8_t)lroundf(clampf3(s->opacity, 0.0f, 1.0f) * 255.0f);
                        uint8_t r = (uint8_t)lroundf(clampf3(s->r, 0.0f, 1.0f) * 255.0f);
                        uint8_t gch = (uint8_t)lroundf(clampf3(s->g, 0.0f, 1.0f) * 255.0f);
                        uint8_t b = (uint8_t)lroundf(clampf3(s->b, 0.0f, 1.0f) * 255.0f);
                        uint32_t src_px = pack_abgr_u8(a, b, gch, r);

                        bool any = false;
                        for (int x = clip_x0; x < clip_x1; x++) {
                                if (wall_depth && depth >= wall_depth[x]) {
                                        continue;
                                }
                                for (int y = clip_y0; y < clip_y1; y++) {
                                        if (depth_pixels) {
                                                float world_depth = depth_pixels[y * fb->width + x];
                                                if (depth >= world_depth) {
                                                        continue;
                                                }
                                        }
                                        float lx = ((float)x + 0.5f - x_center) / (float)radius_px;
                                        float ly = ((float)y + 0.5f - y_center) / (float)radius_px;
                                        float rr = lx * lx + ly * ly;
                                        if (rr > 1.0f) {
                                                continue;
                                        }
                                        // Slightly ragged edges for procedural look.
                                        float edge = 1.0f - rr;
                                        if (edge < 0.0f) {
                                                edge = 0.0f;
                                        }
                                        uint8_t local_a = (uint8_t)lroundf((float)a * edge);
                                        if (local_a == 0u) {
                                                continue;
                                        }
                                        uint32_t tinted = (src_px & 0x00FFFFFFu) | ((uint32_t)local_a << 24u);
                                        uint32_t dst = fb->pixels[y * fb->width + x];
                                        fb->pixels[y * fb->width + x] = blend_abgr8888_over(tinted, dst);
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
