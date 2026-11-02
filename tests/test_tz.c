/* Copyright (C) 2026-present PortareOS */
#include "check.h"
#include "fake_proc.h"
#include "tz.h"

#include <time.h>

static char root[32];

static void zonefile(const char *zone)
{
	/* Directories first: America/Argentina/Buenos_Aires needs two. */
	char p[160];
	snprintf(p, sizeof(p), "%s/zoneinfo/%s", root, zone);
	for (char *s = p + strlen(root) + 1; (s = strchr(s, '/')); s++) {
		*s = '\0';
		mkdir(p, 0755);
		*s = '/';
	}
	fixture_write(p, "TZif");
}

static void test_load(void)
{
	char list[64], zi[64];
	snprintf(list, sizeof(list), "%s/tz", root);
	snprintf(zi, sizeof(zi), "%s/zoneinfo", root);

	/* Include nested zones, aliases, UTC and a zone with no zoneinfo file. */
	fixture_write(list,
		"Europe/Berlin\n"
		"America/New_York\n"
		"America/Argentina/Buenos_Aires\n"
		"US/Pacific\n"
		"Etc/GMT+5\n"
		"Cuba\n"
		"UTC\n"
		"Asia/Tokyo\n"
		"Europe/Amsterdam\r\n"
		"\n");
	zonefile("Europe/Berlin");
	zonefile("Europe/Amsterdam");
	zonefile("America/New_York");
	zonefile("America/Argentina/Buenos_Aires");
	zonefile("US/Pacific");
	zonefile("Etc/GMT+5");
	zonefile("Cuba");
	zonefile("UTC");
	/* No file for Asia/Tokyo. */

	static struct tzlist l;
	CHECK_INT(tz_load(&l, list, zi), 5);
	CHECK_STR(l.zone[0], "America/Argentina/Buenos_Aires");   /* sorted */
	CHECK_STR(l.zone[4], "UTC");

	/* Keep nonempty geographic regions in table order, then UTC; omit aliases. */
	CHECK_INT(l.nregions, 3);
	CHECK_STR(l.region[0], "America");
	CHECK_STR(l.region[1], "Europe");
	CHECK_STR(l.region[2], "UTC");

	int idx[8];
	CHECK_INT(tz_in_region(&l, "Europe", idx, 8), 2);
	CHECK_STR(l.zone[idx[0]], "Europe/Amsterdam");            /* \r gone */
	CHECK_STR(l.zone[idx[1]], "Europe/Berlin");
	CHECK_INT(tz_in_region(&l, "America", idx, 8), 2);
	CHECK_INT(tz_in_region(&l, "UTC", idx, 8), 1);
	CHECK_INT(tz_in_region(&l, "America", idx, 1), 1);        /* capped */
	CHECK_INT(tz_in_region(&l, "Amer", idx, 8), 0);           /* whole word */

	CHECK_INT(tz_load(&l, "/tmp/pl-test-does-not-exist", zi), 0);
	CHECK_INT(l.nregions, 0);
}

static void test_city(void)
{
	char c[48];
	tz_city("America/Argentina/Buenos_Aires", c, sizeof(c));
	CHECK_STR(c, "Argentina/Buenos Aires");
	tz_city("Europe/Isle_of_Man", c, sizeof(c));
	CHECK_STR(c, "Isle of Man");
	tz_city("UTC", c, sizeof(c));
	CHECK_STR(c, "UTC");
}

static void test_clock(void)
{
	/* Compare UTC with gmtime; retry once if the minute rolls over. */
	for (int attempt = 0; attempt < 2; attempt++) {
		char got[8], want[8];
		time_t now = time(NULL);
		struct tm tm;
		gmtime_r(&now, &tm);
		strftime(want, sizeof(want), "%H:%M", &tm);
		tz_clock("UTC", got, sizeof(got));
		if (strcmp(got, want) == 0 || attempt == 1) {
			CHECK_STR(got, want);
			break;
		}
	}

	/* TZ is put back the way it was. */
	setenv("TZ", "Europe/Paris", 1);
	char c[8];
	tz_clock("UTC", c, sizeof(c));
	CHECK_STR(getenv("TZ"), "Europe/Paris");
	unsetenv("TZ");
	tz_clock("UTC", c, sizeof(c));
	CHECK(getenv("TZ") == NULL);
}

static void check_local(time_t when, const char *want, int dst)
{
	struct tm tm;
	CHECK(localtime_r(&when, &tm) != NULL);
	char got[32];
	strftime(got, sizeof(got), "%Y-%m-%d %H:%M:%S", &tm);
	CHECK_STR(got, want);
	CHECK_INT(tm.tm_isdst, dst);
}

static void test_refresh(void)
{
	/* Historical instants just before/after each transition. Keep one zone
	 * loaded across the boundary, as a launcher left running would. */
	static const struct {
		const char *zone;
		time_t at;
		const char *before, *after;
		int dst;
	} transitions[] = {
		{ "Europe/Zurich", 1711846800,
		  "2024-03-31 01:59:59", "2024-03-31 03:00:00", 0 },
		{ "Europe/Zurich", 1729990800,
		  "2024-10-27 02:59:59", "2024-10-27 02:00:00", 1 },
		{ "America/New_York", 1710054000,
		  "2024-03-10 01:59:59", "2024-03-10 03:00:00", 0 },
		{ "America/New_York", 1730613600,
		  "2024-11-03 01:59:59", "2024-11-03 01:00:00", 1 },
		{ "Australia/Sydney", 1712419200,
		  "2024-04-07 02:59:59", "2024-04-07 02:00:00", 1 },
		{ "Australia/Sydney", 1728144000,
		  "2024-10-06 01:59:59", "2024-10-06 03:00:00", 0 },
		/* Not every region changes by a whole hour. */
		{ "Australia/Lord_Howe", 1712415600,
		  "2024-04-07 01:59:59", "2024-04-07 01:30:00", 1 },
		{ "Australia/Lord_Howe", 1728142200,
		  "2024-10-06 01:59:59", "2024-10-06 02:30:00", 0 },
	};
	char cfg[64], text[96], preview[8];
	snprintf(cfg, sizeof(cfg), "%s/system.cfg", root);
	for (size_t i = 0; i < sizeof(transitions) / sizeof(transitions[0]); i++) {
		snprintf(text, sizeof(text), "system.timezone=%s\n", transitions[i].zone);
		fixture_write(cfg, text);
		setenv("TZ", "UTC0", 1);
		tzset();
		tz_refresh(cfg);
		check_local(transitions[i].at - 1, transitions[i].before, transitions[i].dst);
		check_local(transitions[i].at, transitions[i].after, !transitions[i].dst);
		/* Browsing another city's clock restores the selected zone. */
		tz_clock("UTC", preview, sizeof(preview));
		check_local(transitions[i].at, transitions[i].after, !transitions[i].dst);
	}

	/* Refresh a changed setting in the same process. Tokyo and UTC never
	 * make a seasonal adjustment, even when Europe changes its clocks. */
	fixture_write(cfg, "system.timezone=Asia/Tokyo\n");
	tz_refresh(cfg);
	check_local(1711846800, "2024-03-31 10:00:00", 0);
	check_local(1729990800, "2024-10-27 10:00:00", 0);
	fixture_write(cfg, "system.timezone=UTC\n");
	tz_refresh(cfg);
	check_local(1711846800, "2024-03-31 01:00:00", 0);
	check_local(1729990800, "2024-10-27 01:00:00", 0);

	/* Missing/empty settings follow the system, clearing an inherited TZ. */
	tz_refresh("/tmp/pl-test-does-not-exist");
	CHECK(getenv("TZ") == NULL);
	setenv("TZ", "UTC0", 1);
	fixture_write(cfg, "system.timezone=\n");
	tz_refresh(cfg);
	CHECK(getenv("TZ") == NULL);
}

static void test_apply(void)
{
	char cfg[64], cache[64], p[96];
	snprintf(cfg, sizeof(cfg), "%s/system.cfg", root);
	snprintf(cache, sizeof(cache), "%s/cache", root);
	fixture_mkdir(cache);
	fixture_write(cfg, "system.language=en_US\nsystem.timezone=America/New_York\n");

	fake_reset();
	fake_reply("/usr/bin/systemctl restart tz-data.service", "", 0);
	/* A fixed inherited timezone must not override the user's selection. */
	setenv("TZ", "UTC0", 1);
	tzset();
	CHECK_INT(tz_apply("Europe/Berlin", cfg, cache), 0);
	CHECK_STR(getenv("TZ"), ":Europe/Berlin");
	check_local(1711846800, "2024-03-31 03:00:00", 1);
	check_local(1729990800, "2024-10-27 02:00:00", 0);

	CHECK_STR(fixture_read(cfg),
	          "system.language=en_US\nsystem.timezone=Europe/Berlin\n");
	snprintf(p, sizeof(p), "%s/timezone", cache);
	CHECK_STR(fixture_read(p), "TIMEZONE=Europe/Berlin");     /* as es_settings */
	snprintf(p, sizeof(p), "%s/system_timezone", cache);
	CHECK_STR(fixture_read(p), "Europe/Berlin");
	CHECK(fake_called("/usr/bin/systemctl restart tz-data.service"));

	/* Nowhere to write: fails, and says so. */
	CHECK_INT(tz_apply("Europe/Berlin", cfg, "/tmp/pl-test-does-not-exist"), -1);
	unsetenv("TZ");
	tzset();
}

int main(void)
{
	snprintf(root, sizeof(root), "%s", fixture_dir());
	char zi[64];
	snprintf(zi, sizeof(zi), "%s/zoneinfo", root);
	fixture_mkdir(zi);

	test_load();
	test_city();
	test_clock();
	test_refresh();
	test_apply();

	fixture_cleanup(root);
	return check_report("tz");
}
