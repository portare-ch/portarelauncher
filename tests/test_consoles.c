#include "check.h"
#include "consoles.h"

static char cfg[64], shipped[64];

static int index_of(const char *key)
{
	for (int i = 0; i < n_consoles; i++)
		if (strcmp(consoles[i].key, key) == 0)
			return i;
	return -1;
}

static void test_get(void)
{
	int snes = index_of("snes");
	CHECK(snes >= 0);

	/* Absent, empty and anything but 1 are off, as setsettings.sh
	 * makes of them. */
	fixture_write(cfg, "snes.preempt=1\nsnes.integerscale=true\n");
	CHECK_INT(console_get(cfg, snes, CON_PRMPT), 1);
	CHECK_INT(console_get(cfg, snes, CON_INTEGER), 0);
	fixture_write(cfg, "snes.integerscale=\n");
	CHECK_INT(console_get(cfg, snes, CON_PRMPT), 0);
	CHECK_INT(console_get(cfg, snes, CON_INTEGER), 0);

	/* Another system's key, or a game's, is not the console's. */
	fixture_write(cfg, "sfc.integerscale=1\n"
	                   "snes[\"Super Mario World (USA).sfc\"].integerscale=1\n");
	CHECK_INT(console_get(cfg, snes, CON_INTEGER), 0);
	unlink(cfg);
}

static void test_set(void)
{
	int gen = index_of("genesis");
	CHECK(gen >= 0);

	fixture_write(cfg, "genesis.integerscale=1\ngenesis.ratio=4/3\n");
	CHECK_INT(console_set(cfg, gen, CON_INTEGER, 0), 0);
	CHECK_STR(fixture_read(cfg), "genesis.integerscale=0\ngenesis.ratio=4/3\n");
	CHECK_INT(console_set(cfg, gen, CON_PRMPT, 1), 0);
	CHECK_STR(fixture_read(cfg),
	          "genesis.integerscale=0\ngenesis.ratio=4/3\ngenesis.preempt=1\n");
	CHECK_INT(console_get(cfg, gen, CON_INTEGER), 0);
	CHECK_INT(console_get(cfg, gen, CON_PRMPT), 1);
	unlink(cfg);
}

static void test_changed(void)
{
	int snes = index_of("snes"), nes = index_of("nes");

	fixture_write(shipped, "snes.integerscale=1\nsnes.preempt=0\n"
	                       "nes.integerscale=1\nnes.preempt=0\n");

	/* The image's own values are no change, however they are written. */
	fixture_write(cfg, "snes.integerscale=1\nsnes.preempt=0\nnes.integerscale=1\n");
	CHECK_INT(console_changed(cfg, shipped, snes), 0);
	CHECK_INT(console_changed(cfg, shipped, nes), 0);

	fixture_write(cfg, "snes.integerscale=0\nsnes.preempt=1\nnes.integerscale=1\n");
	CHECK_INT(console_changed(cfg, shipped, snes), 2);
	CHECK_INT(console_changed(cfg, shipped, nes), 0);

	/* No shipped copy, as on a build host: every setting is off there. */
	unlink(shipped);
	CHECK_INT(console_changed(cfg, shipped, snes), 1);
	unlink(cfg);
}

int main(void)
{
	char *dir = fixture_dir();
	snprintf(cfg, sizeof(cfg), "%s/system.cfg", dir);
	snprintf(shipped, sizeof(shipped), "%s/shipped.cfg", dir);

	test_get();
	test_set();
	test_changed();

	fixture_cleanup(dir);
	return check_report("consoles");
}
