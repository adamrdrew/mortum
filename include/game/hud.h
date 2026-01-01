#pragma once

#include "game/game_state.h"
#include "game/player.h"
#include "game/font.h"
#include "render/framebuffer.h"

#include "assets/hud_loader.h"

#include "core/config.h"

#include "assets/asset_paths.h"
#include "render/texture.h"

typedef struct HudSystem {
	HudAsset asset;
	bool loaded;

	FontSystem font;
	bool font_loaded;

	const Texture* bar_bg_tex;   // optional cached (if bar.background.mode == image)
	const Texture* panel_bg_tex; // optional cached (if widgets.panel.background.mode == image)

	char hud_filename[64];
} HudSystem;

// Startup: loads and validates the HUD asset referenced by cfg->ui.hud.file.
// Returns false on failure (caller should abort startup).
bool hud_system_init(HudSystem* hud, const CoreConfig* cfg, const AssetPaths* paths, TextureRegistry* texreg);

// Reload: tries to load the HUD asset referenced by cfg->ui.hud.file.
// On failure, keeps the previous HUD state and returns false.
bool hud_system_reload(HudSystem* hud, const CoreConfig* cfg, const AssetPaths* paths, TextureRegistry* texreg);

void hud_system_shutdown(HudSystem* hud);

// Immediate-mode draw (no per-frame allocations, no disk I/O).
void hud_draw(HudSystem* hud, Framebuffer* fb, const Player* player, const GameState* state, int fps, TextureRegistry* texreg, const AssetPaths* paths);
