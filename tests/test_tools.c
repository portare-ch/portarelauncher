#include "check.h"
#include "tools.h"

static void test_folder(void)
{
	char *root = fixture_dir();
	char p[128];

	snprintf(p, sizeof(p), "%s/commander.sh", root);    fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/gamepad-tester.sh", root); fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/zzz-unlisted.sh", root); fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/README.txt", root);      fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/.hidden.sh", root);      fixture_write(p, "");
	snprintf(p, sizeof(p), "%s/folder.sh", root);       fixture_mkdir(p);
	snprintf(p, sizeof(p), "%s/gamelist.xml", root);
	fixture_write(p,
		"<?xml version=\"1.0\"?>\n<gameList>\n"
		"\t<game>\n"
		"\t\t<path>./gamepad-tester.sh</path>\n"
		"\t\t<name>Gamepad Tester</name>\n"
		"\t</game>\n"
		"\t<game>\n"
		"\t\t<path>./commander.sh</path>\n"
		"\t\t<name>File Manager</name>\n"
		"\t\t<desc>Hold SELECT &amp; START to leave &lt;safely&gt;.</desc>\n"
		"\t</game>\n"
		"</gameList>\n");

	struct tools ts;
	CHECK_INT(tools_load_from(&ts, root), 3);
	CHECK_STR(ts.dir, root);
	if (ts.n == 3) {
		/* Sorted by the name shown, not the file name. */
		CHECK_STR(ts.t[0].name, "File Manager");
		CHECK_STR(ts.t[0].file, "commander.sh");
		CHECK_STR(ts.t[0].desc, "Hold SELECT & START to leave <safely>.");
		CHECK_STR(ts.t[1].name, "Gamepad Tester");
		CHECK_STR(ts.t[1].desc, "");
		/* Not in the gamelist: named after the file. */
		CHECK_STR(ts.t[2].name, "zzz-unlisted");
		CHECK_STR(ts.t[2].file, "zzz-unlisted.sh");
	}

	fixture_cleanup(root);
}

static void test_no_gamelist(void)
{
	char *root = fixture_dir();
	char p[128];
	snprintf(p, sizeof(p), "%s/tool.sh", root);
	fixture_write(p, "");

	struct tools ts;
	CHECK_INT(tools_load_from(&ts, root), 1);
	CHECK_STR(ts.t[0].name, "tool");
	fixture_cleanup(root);
}

static void test_missing_folder(void)
{
	struct tools ts;
	CHECK_INT(tools_load_from(&ts, "/tmp/pl-test-does-not-exist"), 0);
	CHECK_INT(ts.n, 0);
}

int main(void)
{
	test_folder();
	test_no_gamelist();
	test_missing_folder();
	return check_report("tools");
}
