# 001 Perf Improvements (Target: 60 FPS)

Goal: reach stable 60fps (16.67ms/frame) on representative maps (start with `big.json`) while keeping visuals “retro/PS1-ish” and keeping the profiling output compact and copy/pasteable.

Constraints:
- Keep perf trace output actionable but short.
- Avoid adding expensive instrumentation by default; only active during perf trace.
- Prefer “big wins” first (remove obvious hot-loop costs) before invasive changes.

---

## Baseline + Repro

- [x] Record baseline on `big.json` at 640x400: run perf trace (`O`) while standing still, then while turning slowly 360°.
- [ ] Record baseline on at least one small/simple map (e.g. `arena.json`) for comparison.
- [x] Save baseline trace outputs in a scratch log (or paste into an issue) for before/after diffing.

---

## Add Actionable Perf Breakdown (instrumentation only)

### Split `render3d` into sub-timings

- [x] Add timing buckets inside the raycaster (only when perf tracing):
  - [ ] `ray_setup_ms` (ray dir + corr computation)
  - [x] `hit_test_ms` (time in wall intersection search)
  - [x] `planes_ms` (floor+ceiling drawing)
  - [x] `walls_ms` (wall span drawing)
  - [x] `tex_lookup_ms` (time spent resolving textures)
  - [ ] `tex_sample_ms` (optional: texture sampling cost)
  - [ ] `lighting_ms` (time spent applying lighting)

### Add high-signal counters (copy/paste friendly)

- [x] Add counters for:
  - [x] `portal_recursions_total` and `portal_max_depth`
  - [x] `texture_get_calls_total` (how often `texture_registry_get()` is called)
  - [x] `registry_string_compares_total` (how many `strncmp` comparisons happen)
  - [x] `pixels_floor`, `pixels_ceil`, `pixels_wall` written

### Add “worst frame” attribution

- [x] In worst-frame line, include the biggest sub-bucket(s) and key counters (recursions, texture lookups).

### Re-run trace

- [x] Run another 60-frame perf trace on `big.json` after instrumentation and capture output.

---

## Prioritized Optimizations (largest wins first)

### Tier 1: Remove obvious hot-loop overhead

- [ ] Eliminate per-column trig where possible:
  - [ ] Replace per-column `sinf/cosf` with incremental rotation across screen columns.
  - [ ] Replace `corr = cosf(ray_rad - cam_rad)` with dot product against camera forward (no trig).

- [ ] Remove texture registry work from inner loops:
  - [ ] Pre-resolve sector floor/ceil textures to pointers for the current frame (or cache per sector until textures change).
  - [ ] Pre-resolve wall textures to pointers (cache per wall index).
  - [ ] Confirm perf trace shows sharp drop in `tex_lookup_ms` and `registry_string_compares_total`.

- [ ] Make nearest sampling cheaper for 64×64 textures:
  - [ ] Convert clamp + float math to fast integer addressing where safe.

### Tier 2: Reduce floor/ceiling cost

- [ ] Reduce per-pixel division:
  - [ ] Precompute row distance factors per scanline (top half for ceiling, bottom half for floor) per frame.

- [ ] Replace `fractf(v) = v - floorf(v)` with a cheaper alternative (avoid libm `floorf` in hot path).

- [ ] Reduce portal overdraw:
  - [ ] Avoid drawing full planes repeatedly for portal recursion (limit drawing to uncovered spans, or draw planes once for the final visible sector span).

### Tier 3: Hit testing acceleration

- [ ] Reduce `find_nearest_wall_hit_in_sector()` cost:
  - [ ] Build per-sector wall lists so we don’t scan all walls every time.
  - [ ] Optional: spatial partition (uniform grid / BSP-lite) if needed.

---

## PS1-Style Cheap Lighting Redesign (aim: perf win or neutral)

Current state: lighting is applied per pixel via `lighting_apply()` which does float multiplies, clamping, and `lroundf`, and optionally loops point lights.

Desired look: PS1-ish / retro: banded shading + dithering and/or “chunky” lighting blocks/triangles.

### Step 1: Confirm constraints

- [ ] Confirm whether point lights are expected to be used soon. (If rarely used, optimize the common `light_count==0` path heavily.)

### Option A (recommended first): Banded lighting + Bayer dithering (very cheap)

- [ ] Implement banded lighting:
  - [ ] Quantize the final multiplier (or final RGB) into N steps (e.g. 8–16 levels).
  - [ ] Make N configurable (compile-time or config).

- [ ] Add ordered dithering:
  - [ ] Use a tiny 4×4 Bayer matrix in screen space.
  - [ ] Dither between band levels to reduce visible banding.
  - [ ] Keep it branch-light and integer-heavy.

- [ ] Ensure this can run in a fast path when `light_count==0` (no loop).

### Option B: Chunky “block lighting” (screen-space tiles)

- [ ] Apply lighting per block instead of per pixel:
  - [ ] Choose a tile size (e.g. 4×4 or 8×8 pixels).
  - [ ] Compute lighting once per tile (at tile center) and reuse for all pixels in the tile.
  - [ ] Combine with banding/dither for PS1 vibe.

- [ ] Validate artifacts are acceptable (retro look).

### Option C: Triangle-ish lighting (optional)

- [ ] If desired, approximate triangle lighting by alternating tile patterns (diagonal split) using a 2×2 or 4×4 pattern.
  - [ ] Only do this if it stays cheap and doesn’t complicate the shader path.

### Validation

- [ ] Run perf trace before/after lighting changes (standing + turning).
- [ ] Confirm `lighting_ms` decreases or stays flat.
- [ ] Confirm overall FPS improves or stays stable.

---

## Verification + Quality Gates

- [ ] Visual sanity: no floor/ceiling “compression” regressions on `big.json` platform.
- [ ] No behavioral regressions: collision/physics unchanged.
- [ ] Perf trace shows:
  - [ ] `frame_ms avg <= 16.7` (or very close with clear remaining hotspots)
  - [ ] `p95` improves meaningfully (less hitching)
- [ ] Build stays clean with `-Werror`.

---

## Notes / Hypotheses to Confirm with Instrumentation

- [ ] `texture_registry_get()` is likely a major hidden cost (linear scan + `strncmp` in hot loop).
- [ ] Floor/ceiling plane drawing likely dominates pixel work (many pixels, heavy float ops + `floorf`).
- [ ] Portal recursion may cause overdraw (planes redrawn for multiple sectors per column).
