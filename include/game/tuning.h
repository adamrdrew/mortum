#pragma once

// US2 tuning constants (kept simple and centralized).

#define MORTUM_HAZARD_RATE_PER_S 12.0f
#define UNDEAD_HEALTH_DRAIN_PER_S 8.0f
#define UNDEAD_SHARDS_REQUIRED 3

#define PURGE_ITEM_MORTUM_REDUCE 35

// Movable sector floor speed (world units per second).
#define MORTUM_SECTOR_FLOOR_SPEED 2.5f

// --- Audio (SFX) tuning ---

// Max concurrent SFX voices mixed at once.
#define SFX_MAX_VOICES 16

// Max distinct WAV samples kept cached at once.
#define SFX_MAX_SAMPLES 128

// Global spatial attenuation distances (world units).
// If distance <= MIN: full gain. If >= MAX: silent. Squared falloff between.
#define SFX_ATTEN_MIN_DIST 6.0f
#define SFX_ATTEN_MAX_DIST 28.0f

