#pragma once

#include <stdbool.h>

// Safe relative path rules (used by Scenes and config/content references):
// - Must be relative (cannot start with '/' or '\\')
// - Must not contain ".."
// - Must not contain backslashes
// - Allowed chars: [A-Za-z0-9_./-]
// This permits subfolders while preventing traversal.
bool name_is_safe_relpath(const char* name);
