#include "game/console.h"

#include "render/draw.h"
#include "core/config.h"

#include <SDL.h>

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static inline ColorRGBA color_from_abgr(uint32_t abgr) {
	ColorRGBA c;
	c.a = (uint8_t)((abgr >> 24) & 0xFFu);
	c.b = (uint8_t)((abgr >> 16) & 0xFFu);
	c.g = (uint8_t)((abgr >> 8) & 0xFFu);
	c.r = (uint8_t)((abgr)&0xFFu);
	return c;
}

static void console_printf(Console* con, const char* fmt, ...) {
	if (!con || !fmt) {
		return;
	}
	char buf[CONSOLE_LINE_MAX];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	console_print(con, buf);
}

static int stricmp_ascii(const char* a, const char* b) {
	if (a == b) {
		return 0;
	}
	if (!a) {
		return -1;
	}
	if (!b) {
		return 1;
	}
	for (;;) {
		unsigned ca = (unsigned char)*a++;
		unsigned cb = (unsigned char)*b++;
		ca = (unsigned)tolower((int)ca);
		cb = (unsigned)tolower((int)cb);
		if (ca != cb) {
			return (ca > cb) - (ca < cb);
		}
		if (ca == 0u) {
			return 0;
		}
	}
}

static bool token_is_space(char ch) {
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
}

// Tokenizer that supports quoted strings with basic backslash escapes.
// Produces argv strings in-place in `scratch`.
static bool console_tokenize(const char* line, char* scratch, size_t scratch_cap, const char** out_argv, int* out_argc, char* out_err, size_t out_err_cap) {
	if (!out_argv || !out_argc) {
		return false;
	}
	*out_argc = 0;
	if (!line) {
		return true;
	}
	if (!scratch || scratch_cap == 0) {
		if (out_err && out_err_cap) {
			snprintf(out_err, out_err_cap, "internal error: no scratch buffer");
		}
		return false;
	}

	size_t w = 0;
	const char* p = line;
	while (*p) {
		while (*p && token_is_space(*p)) {
			p++;
		}
		if (!*p) {
			break;
		}
		if (*out_argc >= CONSOLE_MAX_TOKENS) {
			if (out_err && out_err_cap) {
				snprintf(out_err, out_err_cap, "too many tokens (max %d)", CONSOLE_MAX_TOKENS);
			}
			return false;
		}

		// Start token.
		out_argv[*out_argc] = &scratch[w];
		(*out_argc)++;

		char quote = 0;
		if (*p == '\"' || *p == '\'') {
			quote = *p++;
		}

		while (*p) {
			char ch = *p++;
			if (quote) {
				if (ch == quote) {
					break;
				}
				if (ch == '\\' && *p) {
					char esc = *p++;
					switch (esc) {
						case 'n': ch = '\n'; break;
						case 'r': ch = '\r'; break;
						case 't': ch = '\t'; break;
						case '\\': ch = '\\'; break;
						case '\"': ch = '\"'; break;
						case '\'': ch = '\''; break;
						default: ch = esc; break;
					}
				}
			} else {
				if (token_is_space(ch)) {
					break;
				}
				if (ch == '\\' && *p) {
					// Allow escaping spaces in unquoted strings.
					char next = *p;
					if (token_is_space(next) || next == '\\' || next == '\"' || next == '\'') {
						ch = next;
						p++;
					}
				}
			}

			if (w + 1 >= scratch_cap) {
				if (out_err && out_err_cap) {
					snprintf(out_err, out_err_cap, "input too long");
				}
				return false;
			}
			scratch[w++] = ch;
		}

		if (quote && (*(p - 1) != quote)) {
			if (out_err && out_err_cap) {
				snprintf(out_err, out_err_cap, "unterminated quoted string");
			}
			return false;
		}

		if (w + 1 >= scratch_cap) {
			if (out_err && out_err_cap) {
				snprintf(out_err, out_err_cap, "input too long");
			}
			return false;
		}
		scratch[w++] = '\0';
	}

	return true;
}

static const ConsoleCommand* find_command(const Console* con, const char* name) {
	if (!con || !name) {
		return NULL;
	}
	for (int i = 0; i < con->command_count; i++) {
		if (stricmp_ascii(con->commands[i].name, name) == 0) {
			return &con->commands[i];
		}
	}
	return NULL;
}

void console_init(Console* con) {
	if (!con) {
		return;
	}
	memset(con, 0, sizeof(*con));
	con->open = false;
	con->suppress_text_once = false;
	con->scroll = 0;
	con->line_head = 0;
	con->line_count = 0;
	con->input_len = 0;
	con->command_count = 0;
	con->history_count = 0;
	con->history_pos = -1;
	con->history_has_saved_input = false;
}

bool console_is_open(const Console* con) {
	return con && con->open;
}

void console_set_open(Console* con, bool open) {
	if (!con) {
		return;
	}
	if (con->open == open) {
		return;
	}
	con->open = open;
	// Contents are not preserved between open/close.
	if (!open) {
		console_clear(con);
		con->input_len = 0;
		con->input[0] = '\0';
		con->scroll = 0;
		con->history_pos = -1;
		con->history_has_saved_input = false;
		con->suppress_text_once = false;
	} else {
		console_clear(con);
		con->input_len = 0;
		con->input[0] = '\0';
		con->scroll = 0;
		con->history_pos = -1;
		con->history_has_saved_input = false;
		con->blink_timer = 0.0f;
		con->blink_on = true;
		// Prevent the toggle key's SDL_TEXTINPUT (e.g. '~') from appearing in the console.
		con->suppress_text_once = true;
	}
}

void console_clear(Console* con) {
	if (!con) {
		return;
	}
	con->line_head = 0;
	con->line_count = 0;
	con->scroll = 0;
	for (int i = 0; i < CONSOLE_MAX_LINES; i++) {
		con->lines[i][0] = '\0';
	}
}

static void console_set_input(Console* con, const char* s) {
	if (!con) {
		return;
	}
	if (!s) {
		con->input_len = 0;
		con->input[0] = '\0';
		return;
	}
	strncpy(con->input, s, sizeof(con->input) - 1);
	con->input[sizeof(con->input) - 1] = '\0';
	con->input_len = (int)strlen(con->input);
}

static void console_history_push(Console* con, const char* line) {
	if (!con || !line || !line[0]) {
		return;
	}
	// Avoid consecutive duplicates.
	if (con->history_count > 0) {
		const char* last = con->history[con->history_count - 1];
		if (strcmp(last, line) == 0) {
			return;
		}
	}
	if (con->history_count < CONSOLE_HISTORY_MAX) {
		strncpy(con->history[con->history_count], line, CONSOLE_MAX_INPUT - 1);
		con->history[con->history_count][CONSOLE_MAX_INPUT - 1] = '\0';
		con->history_count++;
		return;
	}
	// Shift left (small buffer, keep it simple).
	for (int i = 1; i < CONSOLE_HISTORY_MAX; i++) {
		strncpy(con->history[i - 1], con->history[i], CONSOLE_MAX_INPUT);
		con->history[i - 1][CONSOLE_MAX_INPUT - 1] = '\0';
	}
	strncpy(con->history[CONSOLE_HISTORY_MAX - 1], line, CONSOLE_MAX_INPUT - 1);
	con->history[CONSOLE_HISTORY_MAX - 1][CONSOLE_MAX_INPUT - 1] = '\0';
}

static void console_history_up(Console* con) {
	if (!con || con->history_count <= 0) {
		return;
	}
	if (con->history_pos < 0) {
		strncpy(con->history_saved_input, con->input, sizeof(con->history_saved_input) - 1);
		con->history_saved_input[sizeof(con->history_saved_input) - 1] = '\0';
		con->history_has_saved_input = true;
		con->history_pos = con->history_count - 1;
	} else if (con->history_pos > 0) {
		con->history_pos--;
	}
	console_set_input(con, con->history[con->history_pos]);
}

static void console_history_down(Console* con) {
	if (!con) {
		return;
	}
	if (con->history_pos < 0) {
		return;
	}
	if (con->history_pos < con->history_count - 1) {
		con->history_pos++;
		console_set_input(con, con->history[con->history_pos]);
		return;
	}
	// Past the newest history entry -> restore saved input.
	con->history_pos = -1;
	if (con->history_has_saved_input) {
		console_set_input(con, con->history_saved_input);
	} else {
		console_set_input(con, "");
	}
}

void console_print(Console* con, const char* line) {
	if (!con || !line) {
		return;
	}
	int slot;
	if (con->line_count < CONSOLE_MAX_LINES) {
		slot = (con->line_head + con->line_count) % CONSOLE_MAX_LINES;
		con->line_count++;
	} else {
		// Drop oldest.
		slot = con->line_head;
		con->line_head = (con->line_head + 1) % CONSOLE_MAX_LINES;
	}
	strncpy(con->lines[slot], line, CONSOLE_LINE_MAX - 1);
	con->lines[slot][CONSOLE_LINE_MAX - 1] = '\0';
}

bool console_register_command(Console* con, ConsoleCommand cmd) {
	if (!con || !cmd.name || !cmd.name[0] || !cmd.fn) {
		return false;
	}
	if (con->command_count >= (int)(sizeof(con->commands) / sizeof(con->commands[0]))) {
		return false;
	}
	con->commands[con->command_count++] = cmd;
	return true;
}

static void console_backspace(Console* con) {
	if (!con || con->input_len <= 0) {
		return;
	}
	// Remove last byte; this is UTF-8 unsafe but acceptable for ASCII console commands.
	con->input_len--;
	con->input[con->input_len] = '\0';
}

static void console_append_text(Console* con, const char* utf8, int len) {
	if (!con || !utf8 || len <= 0) {
		return;
	}
	// Filter control chars; accept visible ASCII and common UTF-8 bytes.
	for (int i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)utf8[i];
		if (ch == '\n' || ch == '\r' || ch == '\t') {
			continue;
		}
		if (ch < 0x20u) {
			continue;
		}
		if (con->input_len + 1 >= (int)sizeof(con->input)) {
			return;
		}
		con->input[con->input_len++] = (char)ch;
		con->input[con->input_len] = '\0';
	}
}

void console_update(Console* con, const Input* in, void* user_ctx) {
	(void)user_ctx;
	if (!con || !in || !con->open) {
		return;
	}

	// Text input for this frame.
	if (con->suppress_text_once) {
		// Drop the first frame of SDL_TEXTINPUT after opening.
		con->suppress_text_once = false;
	} else if (in->text_utf8_len > 0) {
		// Typing means we're editing a line; leave history browse mode.
		con->history_pos = -1;
		console_append_text(con, in->text_utf8, in->text_utf8_len);
	}

	bool submit = false;

	bool shift = input_key_down(in, SDL_SCANCODE_LSHIFT) || input_key_down(in, SDL_SCANCODE_RSHIFT);
	for (int i = 0; i < in->key_event_count; i++) {
		int sc = in->key_events[i].scancode;
		bool rep = in->key_events[i].repeat;
		(void)rep;
		if (sc == SDL_SCANCODE_BACKSPACE) {
			con->history_pos = -1;
			console_backspace(con);
		} else if (sc == SDL_SCANCODE_RETURN) {
			submit = true;
		} else if (sc == SDL_SCANCODE_UP) {
			if (shift) {
				// Scroll output up.
				if (con->scroll < con->line_count) {
					con->scroll++;
				}
			} else {
				console_history_up(con);
			}
		} else if (sc == SDL_SCANCODE_DOWN) {
			if (shift) {
				// Scroll output down.
				if (con->scroll > 0) {
					con->scroll--;
				}
			} else {
				console_history_down(con);
			}
		}
	}

	if (!submit) {
		return;
	}

	// Trim leading spaces.
	const char* line = con->input;
	while (*line && token_is_space(*line)) {
		line++;
	}
	if (!*line) {
		con->input_len = 0;
		con->input[0] = '\0';
		return;
	}

	// Echo.
	console_printf(con, "> %s", line);
	// On submit, follow latest output.
	con->scroll = 0;

	char scratch[512];
	const char* argv[CONSOLE_MAX_TOKENS];
	int argc = 0;
	char err[128];
	err[0] = '\0';
	if (!console_tokenize(line, scratch, sizeof(scratch), argv, &argc, err, sizeof(err))) {
		console_printf(con, "Error: %s", err[0] ? err : "Invalid input");
		con->input_len = 0;
		con->input[0] = '\0';
		return;
	}
	if (argc <= 0) {
		con->input_len = 0;
		con->input[0] = '\0';
		return;
	}

	// Global flag system:
	// - Flags can appear anywhere in the line (before or after the command).
	// - Unknown flags produce a bespoke error.
	// - "--" ends flag parsing; remaining tokens are treated as positional args.
	bool flag_close = false;
	bool stop_flag_parse = false;
	const char* pos_argv[CONSOLE_MAX_TOKENS];
	int pos_argc = 0;
	for (int i = 0; i < argc; i++) {
		const char* t = argv[i];
		if (!t || !t[0]) {
			continue;
		}
		if (!stop_flag_parse && strcmp(t, "--") == 0) {
			stop_flag_parse = true;
			continue;
		}
		if (!stop_flag_parse && t[0] == '-' && t[1] == '-') {
			if (strcmp(t, "--close") == 0) {
				flag_close = true;
				continue;
			}
			console_printf(con, "Error: Unknown flag: %s", t);
			con->input_len = 0;
			con->input[0] = '\0';
			return;
		}
		if (pos_argc < CONSOLE_MAX_TOKENS) {
			pos_argv[pos_argc++] = t;
		}
	}

	if (pos_argc <= 0) {
		console_print(con, "Error: Missing command.");
		con->input_len = 0;
		con->input[0] = '\0';
		return;
	}

	const ConsoleCommand* cmd = find_command(con, pos_argv[0]);
	if (!cmd) {
		console_print(con, "Error: Unknown command.");
		con->input_len = 0;
		con->input[0] = '\0';
		return;
	}

	console_history_push(con, line);

	if (flag_close) {
		// Close first so the game loop resumes immediately; then run command logic.
		console_set_open(con, false);
	}
	(void)cmd->fn(con, pos_argc - 1, &pos_argv[1], user_ctx);
	if (flag_close) {
		// Closing clears output; ensure no post-close output lingers.
		console_clear(con);
	}

	// Clear input after execution.
	con->input_len = 0;
	con->input[0] = '\0';
}

void console_draw(const Console* con, FontSystem* font, Framebuffer* fb) {
	if (!con || !font || !fb || !con->open) {
		return;
	}

	// Quake-style: top half of the screen.
	int h = fb->height / 2;
	if (h < 32) {
		h = fb->height;
	}
	// Get opacity from config, fallback to 0.9 if not available
	float opacity = 0.9f;
	const CoreConfig* cfg = core_config_get();
	if (cfg && cfg->console_opacity >= 0.0f && cfg->console_opacity <= 1.0f) {
		opacity = cfg->console_opacity;
	}
	uint32_t a = (uint32_t)(opacity * 255.0f + 0.5f);
	uint32_t bg = (a << 24);
	draw_rect_abgr8888_alpha(fb, 0, 0, fb->width, h, bg);

	ColorRGBA white = color_from_abgr(0xFFFFFFFFu);
	white.a = (uint8_t)a;
	int line_h = font_line_height(font, 1.0f);
	if (line_h <= 0) {
		line_h = 16;
	}

	int padding = 8;
	int input_y = h - padding - line_h;
	if (input_y < 0) {
		input_y = 0;
	}

	// Input line at bottom with blinking cursor.
	{
		char buf[CONSOLE_LINE_MAX + 4];
		const char* cursor = (con->blink_on ? "_" : " ");
		snprintf(buf, sizeof(buf), "> %s%s", con->input, cursor);
		font_draw_text(font, fb, padding, input_y, buf, white, 1.0f);
	}

	// Output lines above input.
	int max_lines = (input_y - padding) / line_h;
	if (max_lines < 0) {
		max_lines = 0;
	}
	int start = con->line_count - max_lines - con->scroll;
	if (start < 0) {
		start = 0;
	}
	int end = start + max_lines;
	if (end > con->line_count) {
		end = con->line_count;
	}

	int y = padding;
	for (int i = start; i < end; i++) {
		int idx = (con->line_head + i) % CONSOLE_MAX_LINES;
		font_draw_text(font, fb, padding, y, con->lines[idx], white, 1.0f);
		y += line_h;
		if (y + line_h > input_y) {
			break;
		}
	}
}

void console_blink_update(Console* con, float dt) {
	if (!con || !con->open) {
		return;
	}
	if (dt < 0.0f) {
		dt = 0.0f;
	}
	con->blink_timer += dt;
	// 2 Hz blink (0.5s period)
	while (con->blink_timer >= 0.5f) {
		con->blink_timer -= 0.5f;
		con->blink_on = !con->blink_on;
	}
}


