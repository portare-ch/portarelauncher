/* Time zone selection from the image's zone list. Matches es_settings:
 * write system.timezone and both cache files, then restart tz-data.service.
 * RetroArch reapplies the saved setting when launched. */
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
	char zone[TZ_MAX][48];    /* "Europe/Berlin", sorted */
	int n;
	char region[TZ_REGIONS][16];
	int nregions;
};

/* Loads geographic zones and UTC, skipping aliases and missing zoneinfo
 * files. Returns the zone count. */
int tz_load(struct tzlist *l, const char *list, const char *zoneinfo);

/* Indexes into l->zone of the zones in region, in order. */
int tz_in_region(const struct tzlist *l, const char *region, int *idx, int max);

/* "America/Argentina/Buenos_Aires" -> "Argentina/Buenos Aires". */
void tz_city(const char *zone, char *out, size_t osz);

/* The current time in zone as "HH:MM", for choosing a city by its clock. */
void tz_clock(const char *zone, char *out, size_t osz);

/* Applies the zone; returns 0 or -1 on a file write failure.
 * The service restart is asynchronous. */
int tz_apply(const char *zone, const char *settings, const char *cache);

#endif
