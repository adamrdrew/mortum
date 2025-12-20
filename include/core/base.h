#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef MORTUM_ARRAY_COUNT
#define MORTUM_ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#endif

typedef struct Vec2 {
	float x;
	float y;
} Vec2;

static inline Vec2 vec2(float x, float y) {
	Vec2 v;
	v.x = x;
	v.y = y;
	return v;
}

typedef struct StringView {
	const char* data;
	size_t len;
} StringView;

static inline StringView sv(const char* data, size_t len) {
	StringView v;
	v.data = data;
	v.len = len;
	return v;
}
