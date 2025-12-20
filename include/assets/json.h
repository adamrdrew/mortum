#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "core/base.h"

#include "jsmn/jsmn.h"

typedef struct JsonDoc {
	char* text;          // owned
	size_t len;
	jsmntok_t* tokens;   // owned
	int token_count;
} JsonDoc;

bool json_doc_load_file(JsonDoc* self, const char* path);
void json_doc_destroy(JsonDoc* self);

// Token helpers
bool json_token_is_object(const JsonDoc* doc, int tok);
bool json_token_is_array(const JsonDoc* doc, int tok);
bool json_token_is_string(const JsonDoc* doc, int tok);

StringView json_token_sv(const JsonDoc* doc, int tok);

// Object helpers
bool json_object_get(const JsonDoc* doc, int obj_tok, const char* key, int* out_value_tok);

// Value helpers
bool json_get_string(const JsonDoc* doc, int tok, StringView* out);
bool json_get_int(const JsonDoc* doc, int tok, int* out);
bool json_get_double(const JsonDoc* doc, int tok, double* out);

// Array helpers
int json_array_size(const JsonDoc* doc, int arr_tok);
int json_array_nth(const JsonDoc* doc, int arr_tok, int n);
