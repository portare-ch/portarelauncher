#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "check.h"
#include "lists.h"

static char dir[64], file[128];

static int present(const char *path) { return strstr(path, "gone") == NULL; }

static void test_recent(void)
{
	struct recents r;
	snprintf(file, sizeof(file), "%s/recent", dir);
	CHECK_INT(recents_load(&r, file), 0);     /* no file: empty, no error */
	CHECK_INT(r.n, 0);

	recents_add(&r, "snes", "/roms/snes/smw.sfc", 100);
	recents_add(&r, "psx", "/roms/psx/tekken3.chd", 200);
	CHECK_INT(r.n, 2);
	CHECK_STR(r.e[0].path, "/roms/psx/tekken3.chd");   /* newest first */
	CHECK_STR(r.e[1].sys, "snes");

	/* Playing a game again moves it up rather than listing it twice. */
	recents_add(&r, "snes", "/roms/snes/smw.sfc", 300);
	CHECK_INT(r.n, 2);
	CHECK_STR(r.e[0].path, "/roms/snes/smw.sfc");
	CHECK_INT((int)r.e[0].when, 300);

	/* Ten is the cap; the eleventh pushes the oldest out. */
	for (int i = 0; i < 12; i++) {
		char p[64];
		snprintf(p, sizeof(p), "/roms/gba/game%d.gba", i);
		recents_add(&r, "gba", p, 1000 + i);
	}
	CHECK_INT(r.n, RECENT_MAX);
	CHECK_STR(r.e[0].path, "/roms/gba/game11.gba");
	CHECK_STR(r.e[RECENT_MAX - 1].path, "/roms/gba/game2.gba");

	CHECK_INT(recents_save(&r, file), 0);
	struct recents back;
	CHECK_INT(recents_load(&back, file), 0);
	CHECK_INT(back.n, RECENT_MAX);
	CHECK_STR(back.e[0].path, "/roms/gba/game11.gba");
	CHECK_STR(back.e[0].sys, "gba");
	CHECK_INT((int)back.e[0].when, 1011);

	recents_add(&back, "psx", "/roms/psx/gone.chd", 2000);
	CHECK_INT(recents_prune(&back, present), 1);
	CHECK_INT(back.n, RECENT_MAX - 1);           /* one in at the cap, then out */
	CHECK_STR(back.e[0].path, "/roms/gba/game11.gba");
}

static void test_recent_file_shape(void)
{
	struct recents r = { .n = 0 };
	recents_add(&r, "snes", "/roms/snes/a b.sfc", 1700000000LL);
	snprintf(file, sizeof(file), "%s/sub/dir/recent", dir);   /* parents made */
	CHECK_INT(recents_save(&r, file), 0);
	CHECK_STR(fixture_read(file), "1700000000\tsnes\t/roms/snes/a b.sfc\n");

	/* A short or empty line is skipped, not an entry with garbage in it. */
	fixture_write(file, "oops\n\n5\tgba\t/roms/gba/x.gba\n");
	CHECK_INT(recents_load(&r, file), 0);
	CHECK_INT(r.n, 1);
	CHECK_STR(r.e[0].path, "/roms/gba/x.gba");
}

static void test_favourites(void)
{
	struct favs f = { 0 };
	snprintf(file, sizeof(file), "%s/favourites", dir);
	CHECK_INT(favs_load(&f, file), 0);
	CHECK_INT(f.n, 0);

	CHECK_INT(favs_toggle(&f, "/roms/snes/smw.sfc"), 1);
	CHECK_INT(favs_toggle(&f, "/roms/psx/tekken3.chd"), 1);
	CHECK_INT(favs_has(&f, "/roms/snes/smw.sfc"), 1);
	CHECK_INT(favs_has(&f, "/roms/snes/other.sfc"), 0);
	CHECK_INT(favs_toggle(&f, "/roms/snes/smw.sfc"), 0);  /* and off again */
	CHECK_INT(f.n, 1);
	CHECK_STR(f.path[0], "/roms/psx/tekken3.chd");

	/* Past the first allocation, in order. */
	for (int i = 0; i < 40; i++) {
		char p[64];
		snprintf(p, sizeof(p), "/roms/gba/game%02d.gba", i);
		CHECK_INT(favs_toggle(&f, p), 1);
	}
	CHECK_INT(f.n, 41);
	CHECK_STR(f.path[40], "/roms/gba/game39.gba");

	CHECK_INT(favs_save(&f, file), 0);
	struct favs back = { 0 };
	CHECK_INT(favs_load(&back, file), 0);
	CHECK_INT(back.n, 41);
	CHECK_STR(back.path[0], "/roms/psx/tekken3.chd");

	/* A duplicate line in the file is one favourite. */
	fixture_write(file, "/roms/a\n/roms/a\n/roms/gone\n");
	CHECK_INT(favs_load(&back, file), 0);
	CHECK_INT(back.n, 2);
	CHECK_INT(favs_prune(&back, present), 1);
	CHECK_INT(back.n, 1);
	CHECK_STR(back.path[0], "/roms/a");

	favs_free(&f);
	favs_free(&back);
}

static void test_day_label(void)
{
	char out[16];
	/* 2026-10-06 is a Tuesday; noon UTC, with the test's clock in UTC. */
	long long now = 1791288000LL;
	day_label(now - 3600, now, out, sizeof(out));
	CHECK_STR(out, "TODAY");
	day_label(now - 13 * 3600, now, out, sizeof(out));   /* 23:00 the day before */
	CHECK_STR(out, "YESTERDAY");
	day_label(now - 2 * 86400, now, out, sizeof(out));
	CHECK_STR(out, "SUNDAY");
	day_label(now - 6 * 86400, now, out, sizeof(out));
	CHECK_STR(out, "WEDNESDAY");
	day_label(now - 7 * 86400, now, out, sizeof(out));
	CHECK_STR(out, "29 SEP");
	day_label(now - 40 * 86400, now, out, sizeof(out));
	CHECK_STR(out, "27 AUG");
	/* A clock that went backwards still files the game under today. */
	day_label(now + 3600, now, out, sizeof(out));
	CHECK_STR(out, "TODAY");
}

static void test_short_system(void)
{
	char out[8];
	short_system("snes", out, sizeof(out));      CHECK_STR(out, "SNES");
	short_system("psx", out, sizeof(out));       CHECK_STR(out, "PS1");
	short_system("gamecube", out, sizeof(out));  CHECK_STR(out, "GC");
	short_system("dreamcast", out, sizeof(out)); CHECK_STR(out, "DC");
	short_system("snesmsu1", out, sizeof(out));  CHECK_STR(out, "SNES");
	short_system("wonderswan", out, sizeof(out)); CHECK_STR(out, "WOND");
	short_system("c64", out, sizeof(out));       CHECK_STR(out, "C64");
}

int main(void)
{
	setenv("TZ", "UTC", 1);
	tzset();
	strcpy(dir, fixture_dir());
	test_recent();
	test_recent_file_shape();
	test_favourites();
	test_day_label();
	test_short_system();
	fixture_cleanup(dir);
	return check_report("lists");
}
