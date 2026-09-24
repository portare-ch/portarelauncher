#include "check.h"
#include "sheets.h"

static char root[32];

static char *path(const char *name)
{
	static char p[4][128];
	static int i;
	i = (i + 1) % 4;
	snprintf(p[i], sizeof(p[i]), "%s/%s", root, name);
	return p[i];
}

static void test_is(void)
{
	CHECK(sheet_is("a.cue"));
	CHECK(sheet_is("a.CUE"));
	CHECK(sheet_is("a.gdi"));
	CHECK(sheet_is("a.toc"));
	CHECK(sheet_is("a.m3u"));
	CHECK(sheet_is("a.ccd"));
	CHECK(!sheet_is("a.bin"));
	CHECK(!sheet_is("a.chd"));
	CHECK(!sheet_is("cue"));
	CHECK(!sheet_is("dir.cue/file"));
}

static void test_cue(void)
{
	/* Tekken 3 as it is on the test device, CRLF as a Windows tool writes. */
	fixture_write(path("Tekken 3 (USA).cue"),
		"FILE \"Tekken 3 (USA) (Track 1).bin\" BINARY\r\n"
		"  TRACK 01 MODE2/2352\r\n"
		"    INDEX 01 00:00:00\r\n"
		"FILE \"Tekken 3 (USA) (Track 2).bin\" BINARY\r\n"
		"  TRACK 02 AUDIO\r\n"
		"file unquoted.bin BINARY\r\n"
		"REM FILE \"not-a-reference.bin\"\r\n");
	char out[8][512];
	CHECK_INT(sheet_refs(path("Tekken 3 (USA).cue"), out, 8), 3);
	CHECK_STR(out[0], path("Tekken 3 (USA) (Track 1).bin"));
	CHECK_STR(out[1], path("Tekken 3 (USA) (Track 2).bin"));
	CHECK_STR(out[2], path("unquoted.bin"));

	/* Never more than asked for. */
	CHECK_INT(sheet_refs(path("Tekken 3 (USA).cue"), out, 1), 1);
}

static void test_gdi(void)
{
	/* Count first, then track lines; names quoted or not. */
	fixture_write(path("Sonic.gdi"),
		"3\n"
		"1 0 4 2352 track01.bin 0\n"
		"2 756 0 2352 \"track 02.raw\" 0\n"
		"3 45000 4 2352 track03.bin 0\n");
	char out[8][512];
	CHECK_INT(sheet_refs(path("Sonic.gdi"), out, 8), 3);
	CHECK_STR(out[0], path("track01.bin"));
	CHECK_STR(out[1], path("track 02.raw"));
	CHECK_STR(out[2], path("track03.bin"));
}

static void test_m3u(void)
{
	fixture_write(path("FF7.m3u"),
		"#EXTM3U\n"
		"\n"
		"FF7 (Disc 1).cue\n"
		"  FF7 (Disc 2).cue\n"
		"discs\\FF7 (Disc 3).chd\n"
		".\\FF7 (Disc 4).cue\n");
	char out[8][512];
	CHECK_INT(sheet_refs(path("FF7.m3u"), out, 8), 4);
	CHECK_STR(out[0], path("FF7 (Disc 1).cue"));
	CHECK_STR(out[1], path("FF7 (Disc 2).cue"));
	CHECK_STR(out[2], path("discs/FF7 (Disc 3).chd"));   /* \ made / */
	CHECK_STR(out[3], path("FF7 (Disc 4).cue"));         /* .\ dropped */
}

static void test_toc_ccd_missing(void)
{
	fixture_write(path("Game.toc"),
		"CD_ROM\nTRACK MODE1_RAW\nDATAFILE \"game.bin\" 00:10:00\n"
		"TRACK AUDIO\nFILE \"audio.bin\" 0 00:30:00\n");
	char out[8][512];
	CHECK_INT(sheet_refs(path("Game.toc"), out, 8), 2);
	CHECK_STR(out[0], path("game.bin"));
	CHECK_STR(out[1], path("audio.bin"));

	/* CloneCD names nothing inside; its files share the sheet's name. */
	int n = sheet_refs(path("Clone.ccd"), out, 8);
	CHECK(n >= 2);
	CHECK_STR(out[0], path("Clone.img"));
	CHECK_STR(out[1], path("Clone.sub"));

	CHECK_INT(sheet_refs(path("absent.cue"), out, 8), 0);
}

int main(void)
{
	snprintf(root, sizeof(root), "%s", fixture_dir());
	test_is();
	test_cue();
	test_gdi();
	test_m3u();
	test_toc_ccd_missing();
	fixture_cleanup(root);
	return check_report("sheets");
}
