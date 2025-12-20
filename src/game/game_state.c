#include "game/game_state.h"

#include <string.h>

void game_state_init(GameState* self) {
	memset(self, 0, sizeof(*self));
	self->mode = GAME_MODE_PLAYING;
}
