#include "check.h"
#include "catalog.h"

static char root[32], cfg[64], sys_cfg[64];

/* The PlayStation block as the build generates it, only the path changed. */
static const char *es_systems =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<systemList>\n"
	"\t<system>\n"
	"\t\t<name>psx</name>\n"
	"\t\t<fullname>PlayStation</fullname>\n"
	"\t\t<manufacturer>Sony</manufacturer>\n"
	"\t\t<path>%1$s/roms/psx</path>\n"
	"\t\t<extension>.bin .cue .img .mdf .pbp .toc .cbn .m3u .ccd .chd .iso</extension>\n"
	"\t\t<command>/usr/bin/runemu.sh %%ROM%% -P%%SYSTEM%% --core=%%CORE%% --emulator=%%EMULATOR%% --controllers=\"%%CONTROLLERSCONFIG%%\"</command>\n"
	"\t\t<platform>psx</platform>\n"
	"\t\t<emulators>\n"
	"\t\t\t<emulator name=\"retroarch\">\n"
	"\t\t\t\t<cores>\n"
	"\t\t\t\t\t<core>beetle_psx</core>\n"
	"\t\t\t\t\t<core default=\"true\">swanstation</core>\n"
	"\t\t\t\t</cores>\n"
	"\t\t\t</emulator>\n"
	"\t\t</emulators>\n"
	"\t</system>\n"
	/* ES's Tools system: skipped even though its folder has scripts. */
	"\t<system>\n"
	"\t\t<name>tools</name>\n"
	"\t\t<fullname>Tools</fullname>\n"
	"\t\t<path>%1$s/modules</path>\n"
	"\t\t<extension>.sh</extension>\n"
	"\t\t<command>/usr/bin/foot %%ROM%%</command>\n"
	"\t</system>\n"
	/* A folder with only artwork and a gamelist: dropped. */
	"\t<system>\n"
	"\t\t<name>snes</name>\n"
	"\t\t<fullname>Super Nintendo</fullname>\n"
	"\t\t<path>%1$s/roms/snes</path>\n"
	"\t\t<extension>.sfc .smc</extension>\n"
	"\t\t<command>/usr/bin/runemu.sh %%ROM%%</command>\n"
	"\t</system>\n"
	/* Entities, and games one folder down. */
	"\t<system>\n"
	"\t\t<name>ps2</name>\n"
	"\t\t<fullname>PlayStation 2 &amp; Friends</fullname>\n"
	"\t\t<path>%1$s/roms/ps2</path>\n"
	"\t\t<extension>.iso .chd</extension>\n"
	"\t\t<command>/usr/bin/runemu.sh %%ROM%%</command>\n"
	"\t\t<emulators>\n"
	"\t\t\t<emulator name=\"pcsx2\">\n"
	"\t\t\t</emulator>\n"
	"\t\t</emulators>\n"
	"\t</system>\n"
	"</systemList>\n";

static void touch(const char *rel)
{
	char p[256];
	snprintf(p, sizeof(p), "%s/%s", root, rel);
	fixture_write(p, "");
}

static void dir(const char *rel)
{
	char p[256];
	snprintf(p, sizeof(p), "%s/%s", root, rel);
	fixture_mkdir(p);
}

static void build_fixture(void)
{
	char text[4096];
	snprintf(text, sizeof(text), es_systems, root);
	fixture_write(cfg, text);

	dir("roms");
	dir("roms/psx");
	touch("roms/psx/gamma.chd");
	touch("roms/psx/Beta.CUE");                  /* extension in capitals */
	touch("roms/psx/alpha.pbp");
	touch("roms/psx/Ratchet & Clank.iso");
	touch("roms/psx/notes.txt");                 /* not a game */
	touch("roms/psx/.hidden.chd");               /* dotfile */
	touch("roms/psx/gamelist.xml");

	dir("roms/snes");
	touch("roms/snes/gamelist.xml");
	dir("roms/snes/images");
	touch("roms/snes/images/cover.sfc");         /* artwork folders skipped */

	dir("roms/ps2");
	dir("roms/ps2/Game A");
	touch("roms/ps2/Game A/Game A.iso");         /* one level down: found */
	dir("roms/ps2/Game A/extras");
	touch("roms/ps2/Game A/extras/deep.iso");    /* two levels: not */

	dir("modules");
	touch("modules/commander.sh");
}

static const struct psystem *find(const struct catalog *c, const char *name)
{
	for (int i = 0; i < c->n; i++)
		if (strcmp(c->sys[i].name, name) == 0)
			return &c->sys[i];
	return NULL;
}

static void test_systems(void)
{
	struct catalog c;
	CHECK_INT(catalog_load(&c, cfg, sys_cfg), 0);

	/* psx and ps2; tools and the empty snes are gone. By full name. */
	CHECK_INT(c.n, 2);
	CHECK_STR(c.sys[0].fullname, "PlayStation");
	CHECK_STR(c.sys[1].fullname, "PlayStation 2 & Friends");
	CHECK(find(&c, "tools") == NULL);
	CHECK(find(&c, "snes") == NULL);

	const struct psystem *psx = find(&c, "psx");
	CHECK(psx != NULL);
	if (psx) {
		CHECK_STR(psx->core, "swanstation");     /* the default="true" one */
		CHECK_STR(psx->emulator, "retroarch");
		CHECK_STR(psx->launcher, "/usr/bin/runemu.sh");
	}

	const struct psystem *ps2 = find(&c, "ps2");
	CHECK(ps2 != NULL);
	if (ps2) {
		CHECK_STR(ps2->emulator, "pcsx2");
		CHECK_STR(ps2->core, "");
	}
	catalog_free(&c);
}

static void test_games(void)
{
	struct catalog c;
	CHECK_INT(catalog_load(&c, cfg, sys_cfg), 0);

	const struct psystem *psx = find(&c, "psx");
	CHECK(psx != NULL);
	if (psx) {
		/* Sorted without regard to case, extension dropped from the
		 * name and kept in the path. */
		CHECK_INT(psx->ngames, 4);
		if (psx->ngames == 4) {
			CHECK_STR(psx->games[0].name, "alpha");
			CHECK_STR(psx->games[1].name, "Beta");
			CHECK_STR(psx->games[2].name, "gamma");
			CHECK_STR(psx->games[3].name, "Ratchet & Clank");
			char want[128];
			snprintf(want, sizeof(want), "%s/roms/psx/Ratchet & Clank.iso", root);
			CHECK_STR(psx->games[3].path, want);
		}
	}

	const struct psystem *ps2 = find(&c, "ps2");
	CHECK(ps2 != NULL);
	if (ps2) {
		CHECK_INT(ps2->ngames, 1);
		if (ps2->ngames == 1)
			CHECK_STR(ps2->games[0].name, "Game A");
	}
	catalog_free(&c);
	CHECK_INT(c.n, 0);
}

static void test_overrides(void)
{
	/* system.cfg picks the core and emulator over es_systems.cfg. */
	fixture_write(sys_cfg, "psx.core=beetle_psx\npsx.emulator=retroarch\n");
	struct catalog c;
	CHECK_INT(catalog_load(&c, cfg, sys_cfg), 0);
	const struct psystem *psx = find(&c, "psx");
	CHECK(psx != NULL);
	if (psx)
		CHECK_STR(psx->core, "beetle_psx");
	catalog_free(&c);
	unlink(sys_cfg);
}

static void test_failures(void)
{
	struct catalog c;
	char missing[96];
	snprintf(missing, sizeof(missing), "%s/nope.cfg", root);
	CHECK_INT(catalog_load(&c, missing, sys_cfg), -1);

	/* A catalogue with nothing playable is an error, not an empty list. */
	char empty[96];
	snprintf(empty, sizeof(empty), "%s/empty.cfg", root);
	fixture_write(empty, "<systemList>\n</systemList>\n");
	CHECK_INT(catalog_load(&c, empty, sys_cfg), -1);
	catalog_free(&c);
}

int main(void)
{
	snprintf(root, sizeof(root), "%s", fixture_dir());
	snprintf(cfg, sizeof(cfg), "%s/es_systems.cfg", root);
	snprintf(sys_cfg, sizeof(sys_cfg), "%s/system.cfg", root);
	build_fixture();

	test_systems();
	test_games();
	test_overrides();
	test_failures();

	fixture_cleanup(root);
	return check_report("catalog");
}
