/* The time zone: a region, then a city, from the list the image ships.
 *
 * Applied the way es_settings always did it, because that script still runs
 * whenever RetroArch starts and re-applies system.timezone: the setting,
 * /storage/.cache/timezone for tz-data.service, /storage/.cache/
 * system_timezone, and a restart of that service, which points
 * /var/run/localtime at the zone. Writing only the cache would last until
 * the first game.
 */
#ifndef PL_TZ_H
#define PL_TZ_H

#include <stddef.h>

#define TZ_LIST      "/usr/config/system/configs/tz"
#define TZ_ZONEINFO  "/usr/share/zoneinfo"
#define TZ_CACHE     "/storage/.cache"
#define TZ_KEY       "system.timezone"

#define TZ_MAX       640
#define TZ_REGIONS   16

struct tzlist {
	char zone[TZ_MAX][48];    /* "Europe/Berlin", sorted          */
	int n;
	char region[TZ_REGIONS][16];
	int nregions;
};

/* Reads the list, keeping the zones under a geographic region plus UTC.
 * The tz database also carries old aliases - US/Pacific, GB, Etc/GMT+5,
 * Cuba - which name the same zones again, and a zone that the image has no
 * file for is dropped rather than offered and then failing. Returns n. */
int tz_load(struct tzlist *l, const char *list, const char *zoneinfo);

/* Indexes into l->zone of the zones in region, in order. */
int tz_in_region(const struct tzlist *l, const char *region, int *idx, int max);

/* "America/Argentina/Buenos_Aires" -> "Argentina/Buenos Aires". */
void tz_city(const char *zone, char *out, size_t osz);

/* The current time in zone as "HH:MM", for choosing a city by its clock. */
void tz_clock(const char *zone, char *out, size_t osz);

/* Makes zone the system's time zone. Returns 0, or -1 when a file could not
 * be written; the service restart is not waited on for success, since the
 * launcher reads the zone back itself. */
int tz_apply(const char *zone, const char *settings, const char *cache);

#endif
