/* Shared system.cfg key=value settings, also used by setsettings.sh. */
#ifndef PL_SETTINGS_H
#define PL_SETTINGS_H

#include <stddef.h>

#define SETTINGS_PATH "/storage/.config/system/configs/system.cfg"

/* Returns 1 and fills out for a nonempty value; otherwise 0. */
int settings_get(const char *path, const char *key, char *out, size_t osz);

/* Replaces or appends a key, then atomically renames a temporary file
 * to avoid truncation on interrupted writes. */
int settings_set(const char *path, const char *key, const char *value);

#endif
