#include "core/path_safety.h"

#include <ctype.h>
#include <string.h>

bool name_is_safe_relpath(const char* name) {
	if (!name || !name[0]) {
		return false;
	}
	if (strstr(name, "..") != NULL) {
		return false;
	}
	// Allow '/' for subfolders, but disallow absolute paths and backslashes.
	if (name[0] == '/' || name[0] == '\\') {
		return false;
	}
	for (const char* p = name; *p; p++) {
		unsigned char c = (unsigned char)*p;
		if (c == '\\') {
			return false;
		}
		if (!(isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/')) {
			return false;
		}
	}
	return true;
}
