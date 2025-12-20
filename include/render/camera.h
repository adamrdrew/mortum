#pragma once

// Minimal first-person camera for raycasting.
typedef struct Camera {
	float x;
	float y;
	float angle_deg;
	float fov_deg;
} Camera;

Camera camera_make(float x, float y, float angle_deg, float fov_deg);
