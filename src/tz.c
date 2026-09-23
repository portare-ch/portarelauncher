#include "tz.h"
#include "proc.h"
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* The continents and oceans the tz database files its zones under. Anything
 * else in the list is a backward-compatible alias of one of these. */
static const char *const regions[] = {
	"Africa", "America", "Antarctica", "Arctic", "Asia", "Atlantic",
	"Australia", "Europe", "Indian", "Pacific",
};

static int wanted(const char *zone)
{
	if (strcmp(zone, "UTC") == 0)
		return 1;
	for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
		size_t n = strlen(regions[i]);
		if (strncmp(zone, regions[i], n) == 0 && zone[n] == '/' && zone[n + 1])
			return 1;
	}
	return 0;
}

static int by_zone(const void *a, const void *b)
{
	return strcmp((const char *)a, (const char *)b);
}

int tz_load(struct tzlist *l, const char *list, const char *zoneinfo)
{
	memset(l, 0, sizeof(*l));
	FILE *f = fopen(list, "r");
	if (!f)
		return 0;

	char line[128];
	while (l->n < TZ_MAX && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n \t")] = '\0';
		if (!line[0] || strlen(line) >= sizeof(l->zone[0]) || !wanted(line))
			continue;
		char path[256];
		struct stat st;
		snprintf(path, sizeof(path), "%s/%s", zoneinfo, line);
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		str_copy(l->zone[l->n++], sizeof(l->zone[0]), line);
	}
	fclose(f);
	qsort(l->zone, (size_t)l->n, sizeof(l->zone[0]), by_zone);

	/* The regions that have something in them, in the table's order, and
	 * UTC last: it is one zone, not a place. */
	for (size_t i = 0; i < sizeof(regions) / sizeof(regions[0]); i++) {
		int idx;
		if (tz_in_region(l, regions[i], &idx, 1) > 0)
			str_copy(l->region[l->nregions++], sizeof(l->region[0]), regions[i]);
	}
	for (int i = 0; i < l->n; i++)
		if (strcmp(l->zone[i], "UTC") == 0)
			str_copy(l->region[l->nregions++], sizeof(l->region[0]), "UTC");
	return l->n;
}

int tz_in_region(const struct tzlist *l, const char *region, int *idx, int max)
{
	int n = 0;
	size_t len = strlen(region);
	for (int i = 0; i < l->n && n < max; i++) {
		const char *z = l->zone[i];
		if (strcmp(region, "UTC") == 0 ? strcmp(z, "UTC") == 0
		                               : strncmp(z, region, len) == 0 && z[len] == '/')
			idx[n++] = i;
	}
	return n;
}

void tz_city(const char *zone, char *out, size_t osz)
{
	const char *slash = strchr(zone, '/');
	str_copy(out, osz, slash ? slash + 1 : zone);
	for (char *p = out; *p; p++)
		if (*p == '_')
			*p = ' ';
}

void tz_clock(const char *zone, char *out, size_t osz)
{
	/* TZ for one call, then back to the system's. The launcher is one
	 * thread, so nothing else can read the time in between. */
	char *old = getenv("TZ");
	char saved[64] = "";
	if (old)
		str_copy(saved, sizeof(saved), old);

	char tz[64];
	snprintf(tz, sizeof(tz), ":%s", zone);
	setenv("TZ", tz, 1);
	tzset();
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(out, osz, "%H:%M", &tm);

	if (old)
		setenv("TZ", saved, 1);
	else
		unsetenv("TZ");
	tzset();
}

static int write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "w");
	if (!f)
		return -1;
	int ok = fputs(text, f) >= 0;
	return (fclose(f) == 0 && ok) ? 0 : -1;
}

int tz_apply(const char *zone, const char *settings, const char *cache)
{
	char path[256], text[96];

	if (settings_set(settings, TZ_KEY, zone) < 0)
		return -1;

	/* Exactly what es_settings writes, no newline, so the two agree. */
	snprintf(path, sizeof(path), "%s/timezone", cache);
	snprintf(text, sizeof(text), "TIMEZONE=%s", zone);
	if (write_file(path, text) < 0)
		return -1;
	snprintf(path, sizeof(path), "%s/system_timezone", cache);
	if (write_file(path, zone) < 0)
		return -1;

	char *const argv[] = { (char *)"/usr/bin/systemctl", (char *)"restart",
	                       (char *)"tz-data.service", NULL };
	proc_run_for(argv, NULL, NULL, 10000);
	tzset();
	return 0;
}
