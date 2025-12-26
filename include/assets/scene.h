#pragma once

#include <stdbool.h>
#include <stdint.h>

// Scene asset data loaded from JSON (Assets/Scenes/*.json)

typedef enum SceneTextAlign {
	SCENE_TEXT_ALIGN_LEFT = 0,
	SCENE_TEXT_ALIGN_CENTER = 1,
	SCENE_TEXT_ALIGN_RIGHT = 2,
} SceneTextAlign;

typedef struct SceneRGBA8 {
	uint8_t r;
	uint8_t g;
	uint8_t b;
	uint8_t a;
} SceneRGBA8;

typedef struct SceneFade {
	bool enabled;
	int duration_ms;
	SceneRGBA8 from;
} SceneFade;

typedef struct SceneText {
	bool enabled;
	char* text;          // owned; UTF-8
	char* font_file;     // owned; filename in Assets/Fonts/
	int size_px;         // font pixel height
	int atlas_size;      // square atlas size
	SceneRGBA8 color;
	float opacity;       // multiplies color.a
	SceneTextAlign align;
	int x_px;
	int y_px;
	bool scroll;
	float scroll_speed_px_s;
} SceneText;

typedef struct SceneEndCondition {
	int timeout_ms;       // <=0 means disabled
	bool any_key;
	int scancode;         // SDL scancode when any_key==false; <0 means unset
} SceneEndCondition;

typedef struct SceneSfx {
	char* enter_wav; // owned; filename/path relative to Assets/Sounds/Effects/
	char* exit_wav;  // owned; filename/path relative to Assets/Sounds/Effects/
} SceneSfx;

typedef struct SceneMusic {
	char* midi_file;      // owned; filename relative to Assets/Sounds/MIDI/
	char* soundfont_file; // owned; filename relative to Assets/Sounds/SoundFonts/
	bool no_stop;         // if true and midi_file is not set, do not stop any currently playing MIDI
} SceneMusic;

typedef struct Scene {
	char* background_png; // owned; path relative to Assets/Images/
	SceneMusic music;
	SceneSfx sfx;
	SceneText text;
	SceneEndCondition end;
	SceneFade fade_in;
	SceneFade fade_out;
} Scene;

void scene_destroy(Scene* self);
