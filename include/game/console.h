#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "platform/input.h"
#include "render/framebuffer.h"
#include "game/font.h"

// Quake-style in-game console.
//
// - White text on black background.
// - Output scrolls upward; Shift+Up/Down scrolls the view. Lines that scroll off the top are lost.
// - Dedicated input line at the bottom of the console area.
// - Contents are not preserved between open/close.
//
// Extendability: register commands with console_register_command().

#define CONSOLE_MAX_INPUT 256
#define CONSOLE_MAX_LINES 256
#define CONSOLE_LINE_MAX 256
#define CONSOLE_MAX_TOKENS 16

#define CONSOLE_HISTORY_MAX 16

typedef struct Console Console;

typedef bool (*ConsoleCommandFn)(Console* con, int argc, const char** argv, void* user_ctx);

typedef struct ConsoleCommand {
	const char* name;        // command identifier, e.g. "load_map"
	const char* description; // one-line description shown in help
	const char* example;     // example usage (optional)
	const char* syntax;      // syntax string (optional), e.g. "load_map string"
	ConsoleCommandFn fn;
} ConsoleCommand;

struct Console {
	bool open;
	bool suppress_text_once;

	// Scrollback: number of lines scrolled up from the bottom (0 = follow latest).
	int scroll;

	char input[CONSOLE_MAX_INPUT];
	int input_len;

	// For blinking cursor
	float blink_timer;
	bool blink_on;

	// Command history (most recent at history[history_count-1]).
	char history[CONSOLE_HISTORY_MAX][CONSOLE_MAX_INPUT];
	int history_count;
	int history_pos; // -1 = not browsing history
	char history_saved_input[CONSOLE_MAX_INPUT];
	bool history_has_saved_input;

	// Ring buffer of output lines.
	char lines[CONSOLE_MAX_LINES][CONSOLE_LINE_MAX];
	int line_head;  // index of oldest line
	int line_count; // number of valid lines

	ConsoleCommand commands[64];
	int command_count;
};

void console_init(Console* con);

bool console_is_open(const Console* con);

// Opens/closes the console. When closing, output and input are cleared.
void console_set_open(Console* con, bool open);

// Clears output lines only.
void console_clear(Console* con);

// Adds a line to the output buffer.
void console_print(Console* con, const char* line);

// Registers a command. Returns false if table is full or cmd is invalid.
bool console_register_command(Console* con, ConsoleCommand cmd);

// Updates console input state for this frame (only meaningful when open).
// - Consumes in->text_utf8 and key presses for editing and submission.
// - On Enter: echoes "> <line>" then dispatches to commands.
void console_update(Console* con, const Input* in, void* user_ctx);

// Executes a command line programmatically using the same tokenizer/dispatch path as console_update().
// Returns true if a known command was found and invoked.
bool console_execute_line(Console* con, const char* line, void* user_ctx);

// Renders the console overlay if open.
void console_draw(const Console* con, FontSystem* font, Framebuffer* fb);
// Call this once per frame to update the blink timer (dt in seconds)
void console_blink_update(Console* con, float dt);
