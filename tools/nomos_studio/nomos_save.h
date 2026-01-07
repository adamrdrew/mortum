// Nomos Studio - Map saving header
#ifndef NOMOS_SAVE_H
#define NOMOS_SAVE_H

#include "assets/map_loader.h"

#include <stdbool.h>

// Save a MapLoadResult to a JSON file
// Returns true on success, false on error (check errno or logs)
bool nomos_save_map(const MapLoadResult* map, const char* filepath);

#endif // NOMOS_SAVE_H
