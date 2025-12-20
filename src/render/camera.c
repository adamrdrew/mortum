#include "render/camera.h"

Camera camera_make(float x, float y, float angle_deg, float fov_deg) {
	Camera c;
	c.x = x;
	c.y = y;
	c.angle_deg = angle_deg;
	c.fov_deg = fov_deg;
	return c;
}
