#pragma once

#include "game/world.h"

typedef struct CollisionMoveResult {
	float out_x;
	float out_y;
	bool collided;
} CollisionMoveResult;

// Resolve movement of a circle against solid walls (walls with back_sector == -1).
// Returns a resolved position after at most a small number of iterations.
CollisionMoveResult collision_move_circle(const World* world, float radius, float from_x, float from_y, float to_x, float to_y);
