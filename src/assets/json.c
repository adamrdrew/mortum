#include "assets/json.h"

#include "core/log.h"

#include "jsmn/jsmn.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// -------------------- minimal tokenizer implementation --------------------

void jsmn_init(jsmn_parser* parser) {
	parser->pos = 0;
	parser->toknext = 0;
	parser->toksuper = -1;
}

static jsmntok_t* tok_alloc(jsmn_parser* p, jsmntok_t* tokens, unsigned int num_tokens) {
	if (p->toknext >= num_tokens) {
		return NULL;
	}
	jsmntok_t* t = &tokens[p->toknext++];
	t->type = JSMN_UNDEFINED;
	t->start = -1;
	t->end = -1;
	t->size = 0;
	t->parent = -1;
	return t;
}

static void tok_inc_size(jsmntok_t* tokens, int toksuper) {
	if (toksuper >= 0) {
		tokens[toksuper].size++;
	}
}

static int find_open_token(jsmn_parser* p, jsmntok_t* tokens, jsmntype_t type) {
	for (int i = (int)p->toknext - 1; i >= 0; i--) {
		if (tokens[i].type == type && tokens[i].start != -1 && tokens[i].end == -1) {
			return i;
		}
	}
	return -1;
}

static int parse_string(jsmn_parser* p, const char* js, size_t len, jsmntok_t* tokens, unsigned int num_tokens) {
	unsigned int start = p->pos + 1;
	p->pos++;
	for (; p->pos < len; p->pos++) {
		char c = js[p->pos];
		if (c == '"') {
			jsmntok_t* t = tok_alloc(p, tokens, num_tokens);
			if (!t) {
				return JSMN_ERROR_NOMEM;
			}
			t->type = JSMN_STRING;
			t->start = (int)start;
			t->end = (int)p->pos;
			t->parent = p->toksuper;
			tok_inc_size(tokens, p->toksuper);
			return 0;
		}
		if (c == '\\') {
			// skip escaped character
			p->pos++;
			if (p->pos >= len) {
				return JSMN_ERROR_PART;
			}
		}
	}
	return JSMN_ERROR_PART;
}

static int parse_primitive(jsmn_parser* p, const char* js, size_t len, jsmntok_t* tokens, unsigned int num_tokens) {
	unsigned int start = p->pos;
	for (; p->pos < len; p->pos++) {
		char c = js[p->pos];
		if (c == ',' || c == ']' || c == '}' || isspace((unsigned char)c)) {
			break;
		}
	}
	jsmntok_t* t = tok_alloc(p, tokens, num_tokens);
	if (!t) {
		return JSMN_ERROR_NOMEM;
	}
	t->type = JSMN_PRIMITIVE;
	t->start = (int)start;
	t->end = (int)p->pos;
	t->parent = p->toksuper;
	tok_inc_size(tokens, p->toksuper);
	p->pos--; // outer loop will advance
	return 0;
}

int jsmn_parse(jsmn_parser* p, const char* js, size_t len, jsmntok_t* tokens, unsigned int num_tokens) {
	for (; p->pos < len; p->pos++) {
		char c = js[p->pos];
		if (isspace((unsigned char)c)) {
			continue;
		}

		switch (c) {
			case '{': {
				jsmntok_t* t = tok_alloc(p, tokens, num_tokens);
				if (!t) {
					return JSMN_ERROR_NOMEM;
				}
				t->type = JSMN_OBJECT;
				t->start = (int)p->pos;
				t->parent = p->toksuper;
				tok_inc_size(tokens, p->toksuper);
				p->toksuper = (int)(p->toknext - 1);
			} break;
			case '[': {
				jsmntok_t* t = tok_alloc(p, tokens, num_tokens);
				if (!t) {
					return JSMN_ERROR_NOMEM;
				}
				t->type = JSMN_ARRAY;
				t->start = (int)p->pos;
				t->parent = p->toksuper;
				tok_inc_size(tokens, p->toksuper);
				p->toksuper = (int)(p->toknext - 1);
			} break;
			case '}': {
				int i = find_open_token(p, tokens, JSMN_OBJECT);
				if (i < 0) {
					return JSMN_ERROR_INVAL;
				}
				tokens[i].end = (int)p->pos + 1;
				p->toksuper = tokens[i].parent;
			} break;
			case ']': {
				int i = find_open_token(p, tokens, JSMN_ARRAY);
				if (i < 0) {
					return JSMN_ERROR_INVAL;
				}
				tokens[i].end = (int)p->pos + 1;
				p->toksuper = tokens[i].parent;
			} break;
			case '"': {
				int r = parse_string(p, js, len, tokens, num_tokens);
				if (r < 0) {
					return r;
				}
			} break;
			case ':':
			case ',':
				break;
			default: {
				int r = parse_primitive(p, js, len, tokens, num_tokens);
				if (r < 0) {
					return r;
				}
			} break;
		}
	}

	// sanity: close any open containers
	for (int i = 0; i < (int)p->toknext; i++) {
		if ((tokens[i].type == JSMN_OBJECT || tokens[i].type == JSMN_ARRAY) && tokens[i].end == -1) {
			return JSMN_ERROR_PART;
		}
	}

	return (int)p->toknext;
}

// -------------------- JsonDoc helpers --------------------

static char* read_entire_file(const char* path, size_t* out_len) {
	FILE* f = fopen(path, "rb");
	if (!f) {
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return NULL;
	}
	long sz = ftell(f);
	if (sz < 0) {
		fclose(f);
		return NULL;
	}
	rewind(f);

	char* buf = (char*)malloc((size_t)sz + 1);
	if (!buf) {
		fclose(f);
		return NULL;
	}
	size_t n = fread(buf, 1, (size_t)sz, f);
	fclose(f);
	if (n != (size_t)sz) {
		free(buf);
		return NULL;
	}
	buf[n] = '\0';
	if (out_len) {
		*out_len = n;
	}
	return buf;
}

bool json_doc_load_file(JsonDoc* self, const char* path) {
	self->text = NULL;
	self->len = 0;
	self->tokens = NULL;
	self->token_count = 0;

	size_t len = 0;
	char* text = read_entire_file(path, &len);
	if (!text) {
		log_error("Failed to read JSON file: %s", path);
		return false;
	}

	// Allocate a generous token buffer (simple heuristic).
	unsigned int max_tokens = (unsigned int)(len / 8 + 64);
	jsmntok_t* toks = (jsmntok_t*)calloc(max_tokens, sizeof(jsmntok_t));
	if (!toks) {
		free(text);
		return false;
	}

	jsmn_parser p;
	jsmn_init(&p);
	int r = jsmn_parse(&p, text, len, toks, max_tokens);
	if (r < 0) {
		log_error("JSON parse failed (%d) for %s", r, path);
		free(toks);
		free(text);
		return false;
	}

	self->text = text;
	self->len = len;
	self->tokens = toks;
	self->token_count = r;
	return true;
}

void json_doc_destroy(JsonDoc* self) {
	free(self->text);
	free(self->tokens);
	self->text = NULL;
	self->tokens = NULL;
	self->len = 0;
	self->token_count = 0;
}

static const jsmntok_t* tok_at(const JsonDoc* doc, int tok) {
	if (tok < 0 || tok >= doc->token_count) {
		return NULL;
	}
	return &doc->tokens[tok];
}

bool json_token_is_object(const JsonDoc* doc, int tok) {
	const jsmntok_t* t = tok_at(doc, tok);
	return t && t->type == JSMN_OBJECT;
}

bool json_token_is_array(const JsonDoc* doc, int tok) {
	const jsmntok_t* t = tok_at(doc, tok);
	return t && t->type == JSMN_ARRAY;
}

bool json_token_is_string(const JsonDoc* doc, int tok) {
	const jsmntok_t* t = tok_at(doc, tok);
	return t && t->type == JSMN_STRING;
}

StringView json_token_sv(const JsonDoc* doc, int tok) {
	const jsmntok_t* t = tok_at(doc, tok);
	if (!t || t->start < 0 || t->end < 0 || (size_t)t->end > doc->len) {
		return sv("", 0);
	}
	return sv(doc->text + t->start, (size_t)(t->end - t->start));
}

static bool sv_eq_cstr(StringView a, const char* b) {
	size_t blen = strlen(b);
	return a.len == blen && memcmp(a.data, b, blen) == 0;
}

// For a container token at `tok`, returns index of next token after its subtree.
static int tok_next(const JsonDoc* doc, int tok) {
	const jsmntok_t* t = tok_at(doc, tok);
	if (!t) {
		return tok + 1;
	}
	if (t->type == JSMN_STRING || t->type == JSMN_PRIMITIVE) {
		return tok + 1;
	}
	int i = tok + 1;
	for (int n = 0; n < t->size; n++) {
		// For objects, size counts key+value tokens as separate children.
		i = tok_next(doc, i);
	}
	return i;
}

bool json_object_get(const JsonDoc* doc, int obj_tok, const char* key, int* out_value_tok) {
	if (!json_token_is_object(doc, obj_tok)) {
		return false;
	}
	const jsmntok_t* obj = tok_at(doc, obj_tok);
	// Our tokenizer increments object.size for both key and value tokens.
	// Treat size as "number of child tokens", so object pair count is size/2.
	int pair_count = obj ? (obj->size / 2) : 0;
	int i = obj_tok + 1;
	for (int pair = 0; pair < pair_count; pair++) {
		int key_tok = i;
		int val_tok = i + 1;
		StringView k = json_token_sv(doc, key_tok);
		if (sv_eq_cstr(k, key)) {
			if (out_value_tok) {
				*out_value_tok = val_tok;
			}
			return true;
		}
		i = tok_next(doc, val_tok);
	}
	return false;
}

bool json_get_string(const JsonDoc* doc, int tok, StringView* out) {
	if (!json_token_is_string(doc, tok)) {
		return false;
	}
	if (out) {
		*out = json_token_sv(doc, tok);
	}
	return true;
}

bool json_get_int(const JsonDoc* doc, int tok, int* out) {
	double d = 0.0;
	if (!json_get_double(doc, tok, &d)) {
		return false;
	}
	if (out) {
		*out = (int)d;
	}
	return true;
}

bool json_get_double(const JsonDoc* doc, int tok, double* out) {
	const jsmntok_t* t = tok_at(doc, tok);
	if (!t || t->type != JSMN_PRIMITIVE) {
		return false;
	}
	StringView s = json_token_sv(doc, tok);
	char tmp[64];
	size_t n = s.len < sizeof(tmp) - 1 ? s.len : sizeof(tmp) - 1;
	memcpy(tmp, s.data, n);
	tmp[n] = '\0';
	errno = 0;
	char* endp = NULL;
	double v = strtod(tmp, &endp);
	if (errno != 0 || endp == tmp) {
		return false;
	}
	if (out) {
		*out = v;
	}
	return true;
}

int json_array_size(const JsonDoc* doc, int arr_tok) {
	const jsmntok_t* t = tok_at(doc, arr_tok);
	if (!t || t->type != JSMN_ARRAY) {
		return -1;
	}
	return t->size;
}

int json_array_nth(const JsonDoc* doc, int arr_tok, int n) {
	const jsmntok_t* a = tok_at(doc, arr_tok);
	if (!a || a->type != JSMN_ARRAY || n < 0 || n >= a->size) {
		return -1;
	}
	int i = arr_tok + 1;
	for (int k = 0; k < n; k++) {
		i = tok_next(doc, i);
	}
	return i;
}
