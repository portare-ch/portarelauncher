#include "check.h"
#include "settings.h"

static char path[64];

static void test_missing_file(void)
{
	char v[32] = "untouched";
	CHECK_INT(settings_get(path, "a", v, sizeof(v)), 0);

	/* Setting a key creates the file. */
	CHECK_INT(settings_set(path, "a", "1"), 0);
	CHECK_STR(fixture_read(path), "a=1\n");
	CHECK_INT(settings_get(path, "a", v, sizeof(v)), 1);
	CHECK_STR(v, "1");
	unlink(path);
}

static void test_replace_in_place(void)
{
	fixture_write(path, "# comment\nfirst=1\nwifi.ssid=Home\nlast=3\n");
	CHECK_INT(settings_set(path, "wifi.ssid", "Cafe"), 0);
	CHECK_STR(fixture_read(path), "# comment\nfirst=1\nwifi.ssid=Cafe\nlast=3\n");

	/* The first line has no newline in front of it, so it is found by a
	 * different path than every other line. */
	fixture_write(path, "first=1\nsecond=2\n");
	char v[32];
	CHECK_INT(settings_get(path, "first", v, sizeof(v)), 1);
	CHECK_STR(v, "1");
	CHECK_INT(settings_set(path, "first", "9"), 0);
	CHECK_STR(fixture_read(path), "first=9\nsecond=2\n");
	unlink(path);
}

static void test_append(void)
{
	/* No newline at the end of the last line: appending must not glue
	 * the new key onto it. */
	fixture_write(path, "a=1");
	CHECK_INT(settings_set(path, "b", "2"), 0);
	CHECK_STR(fixture_read(path), "a=1\nb=2\n");
	unlink(path);
}

static void test_prefix_is_not_a_match(void)
{
	/* "psx.core" must not find or overwrite "psx.core_options". */
	fixture_write(path, "psx.core_options=keep\n");
	char v[32];
	CHECK_INT(settings_get(path, "psx.core", v, sizeof(v)), 0);
	CHECK_INT(settings_set(path, "psx.core", "swanstation"), 0);
	CHECK_STR(fixture_read(path), "psx.core_options=keep\npsx.core=swanstation\n");

	/* Nor a key that merely ends the same way. */
	fixture_write(path, "global.psx.core=a\n");
	CHECK_INT(settings_get(path, "psx.core", v, sizeof(v)), 0);
	unlink(path);
}

static void test_values(void)
{
	char v[8];

	fixture_write(path, "k=a=b\n");                 /* '=' in the value */
	CHECK_INT(settings_get(path, "k", v, sizeof(v)), 1);
	CHECK_STR(v, "a=b");

	fixture_write(path, "k=v \r\n");                /* trailing space, CRLF */
	CHECK_INT(settings_get(path, "k", v, sizeof(v)), 1);
	CHECK_STR(v, "v");

	fixture_write(path, "k=\n");                    /* empty is "not set" */
	CHECK_INT(settings_get(path, "k", v, sizeof(v)), 0);

	fixture_write(path, "k=0123456789\n");          /* clipped to fit */
	CHECK_INT(settings_get(path, "k", v, sizeof(v)), 1);
	CHECK_STR(v, "0123456");
	unlink(path);
}

static void test_no_temp_left(void)
{
	fixture_write(path, "a=1\n");
	CHECK_INT(settings_set(path, "a", "2"), 0);
	char tmp[80];
	snprintf(tmp, sizeof(tmp), "%s.pl-tmp", path);
	CHECK(access(tmp, F_OK) != 0);
	unlink(path);
}

static void test_unwritable(void)
{
	/* A directory that does not exist: the write fails, and says so. */
	char bad[96];
	snprintf(bad, sizeof(bad), "%s/missing/system.cfg", path);
	CHECK_INT(settings_set(bad, "a", "1"), -1);
}

int main(void)
{
	char *dir = fixture_dir();
	snprintf(path, sizeof(path), "%s/system.cfg", dir);

	test_missing_file();
	test_replace_in_place();
	test_append();
	test_prefix_is_not_a_match();
	test_values();
	test_no_temp_left();
	test_unwritable();

	fixture_cleanup(dir);
	return check_report("settings");
}
