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

	/* The shape of the image's list: zones, nested zones, aliases, a
	 * single-word zone, and one the image has no file for. */
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

	/* Regions that have zones, in table order, UTC last. No Asia: its
	 * only zone had no file. No US, Etc or Cuba: aliases. */
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
	/* UTC against gmtime, retried once in case a minute turns over
	 * between the two readings. */
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

static void test_apply(void)
{
	char cfg[64], cache[64], p[96];
	snprintf(cfg, sizeof(cfg), "%s/system.cfg", root);
	snprintf(cache, sizeof(cache), "%s/cache", root);
	fixture_mkdir(cache);
	fixture_write(cfg, "system.language=en_US\nsystem.timezone=America/New_York\n");

	fake_reset();
	fake_reply("/usr/bin/systemctl restart tz-data.service", "", 0);
	CHECK_INT(tz_apply("Europe/Berlin", cfg, cache), 0);

	CHECK_STR(fixture_read(cfg),
	          "system.language=en_US\nsystem.timezone=Europe/Berlin\n");
	snprintf(p, sizeof(p), "%s/timezone", cache);
	CHECK_STR(fixture_read(p), "TIMEZONE=Europe/Berlin");     /* as es_settings */
	snprintf(p, sizeof(p), "%s/system_timezone", cache);
	CHECK_STR(fixture_read(p), "Europe/Berlin");
	CHECK(fake_called("/usr/bin/systemctl restart tz-data.service"));

	/* Nowhere to write: fails, and says so. */
	CHECK_INT(tz_apply("Europe/Berlin", cfg, "/tmp/pl-test-does-not-exist"), -1);
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
	test_apply();

	fixture_cleanup(root);
	return check_report("tz");
}
