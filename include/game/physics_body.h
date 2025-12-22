#pragma once

#include <stdbool.h>

#include "game/world.h"

typedef struct PhysicsStepUpState {
	bool active;
	float start_z;
	float target_z;
	float t;
	float duration_s;
	int from_sector;
	int to_sector;
	float total_dx;
	float total_dy;
	float applied_frac;
} PhysicsStepUpState;

// Generic kinematic body for entities (player now, enemies later).
// Coordinates:
// - (x,y) are in map/world units.
// - z is the body's *feet* (bottom) height in world space.
typedef struct PhysicsBody {
	float x;
	float y;
	float z;
	float vx;
	float vy;
	float vz;

	float radius;
	float height;
	float step_height;

	bool on_ground;
	int sector;            // current sector index, or -1
	int last_valid_sector; // last known valid sector index, or -1

	PhysicsStepUpState step_up;
} PhysicsBody;

typedef struct PhysicsBodyParams {
	float gravity_z;          // negative value, world-units / s^2
	float floor_epsilon;      // tolerance for considering "on floor"
	float headroom_epsilon;   // clearance from ceiling
	float step_duration_s;    // duration of step-up animation
	float max_substep_dist;   // maximum horizontal distance per substep
	int max_solve_iterations; // collision iterations per substep
} PhysicsBodyParams;

PhysicsBodyParams physics_body_params_default(void);

void physics_body_init(PhysicsBody* b, float x, float y, float z, float radius, float height, float step_height);

// Update body using desired horizontal velocity (wish_vx/wish_vy).
// Handles:
// - height-aware wall/portal collision
// - falling (gravity), with guaranteed floor clamping
// - step-up animation when entering a slightly higher floor (<= step_height)
void physics_body_update(PhysicsBody* b, const World* world, float wish_vx, float wish_vy, double dt_s, const PhysicsBodyParams* params);

// One-off movement helper (used by dash) that applies the same collision rules.
void physics_body_move_delta(PhysicsBody* b, const World* world, float dx, float dy, const PhysicsBodyParams* params);
