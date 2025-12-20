#pragma once

/*
  Minimal JSON tokenizer for Mortum.

  This is a tiny, allocation-free tokenizer intended for simple config/map JSON.
  It is NOT a full validating parser; it produces a token stream over the input.

  API intentionally mirrors a common tiny-tokenizer shape:
  - `jsmn_parser` holds state
  - `jsmntok_t` describes spans and types
  - `jsmn_parse` tokenizes into caller-provided token array
*/

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	JSMN_UNDEFINED = 0,
	JSMN_OBJECT = 1,
	JSMN_ARRAY = 2,
	JSMN_STRING = 3,
	JSMN_PRIMITIVE = 4
} jsmntype_t;

typedef struct {
	jsmntype_t type;
	int start;
	int end;
	int size;
	int parent;
} jsmntok_t;

typedef struct {
	unsigned int pos;
	unsigned int toknext;
	int toksuper;
} jsmn_parser;

typedef enum {
	JSMN_ERROR_NOMEM = -1,
	JSMN_ERROR_INVAL = -2,
	JSMN_ERROR_PART = -3
} jsmnerr_t;

void jsmn_init(jsmn_parser* parser);
int jsmn_parse(jsmn_parser* parser, const char* js, size_t len, jsmntok_t* tokens, unsigned int num_tokens);

#ifdef __cplusplus
}
#endif
