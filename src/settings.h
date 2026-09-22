/* Reading and writing system.cfg.
 *
 * The same flat key=value file setsettings.sh reads, so anything set here is
 * seen by the rest of the system and anything set elsewhere is seen here.
 * There is no second configuration store and there should not be one.
 */
#ifndef PL_SETTINGS_H
#define PL_SETTINGS_H

#include <stddef.h>

#define SETTINGS_PATH "/storage/.config/system/configs/system.cfg"

/* Returns 1 and fills out on success. Missing key, missing file and empty
 * value are all "not set". */
int settings_get(const char *path, const char *key, char *out, size_t osz);

/* Replaces the key in place if present, appends it if not. Writes through a
 * temporary file and renames, so an interrupted write cannot leave the
 * settings truncated - this file holds every per-system choice on the
 * device. */
int settings_set(const char *path, const char *key, const char *value);

#endif
