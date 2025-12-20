#pragma once

typedef enum GameMode {
	GAME_MODE_PLAYING = 0,
	GAME_MODE_WIN = 1,
	GAME_MODE_LOSE = 2,
} GameMode;

typedef struct GameState {
	GameMode mode;
} GameState;

void game_state_init(GameState* self);
