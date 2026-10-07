#include "check.h"
#include "glyph.h"
#include "lang.h"

/* A format as it is drawn: %c is a key letter, one column; %d a number,
 * three at most where these are used; %s is filled from another entry and
 * checked where the two are put together. */
static int fmt_width(const char *f)
{
	char buf[512];
	size_t n = 0;
	int extra = 0;
	for (const char *p = f; *p && n + 4 < sizeof(buf); p++) {
		if (*p == '%' && p[1]) {
			switch (*++p) {
			case 'c': buf[n++] = 'X'; break;
			case 'd': extra += 3; break;
			case 's': break;
			case '%': buf[n++] = '%'; break;
			}
			continue;
		}
		buf[n++] = *p;
	}
	buf[n] = '\0';
	return text_width(buf) + extra;
}

/* The conversions of a format, in order: "cds" for "%c %d %s". */
static void convs(const char *f, char *out, size_t osz)
{
	size_t n = 0;
	for (const char *p = f; *p && n + 1 < osz; p++)
		if (*p == '%' && p[1]) {
			p++;
			if (*p != '%')
				out[n++] = *p;
		}
	out[n] = '\0';
}

static void test_table(void)
{
	for (int s = 0; s < N_STR; s++) {
		const char *en = tr_in(LANG_EN, (enum str)s);
		const char *ja = tr_in(LANG_JA, (enum str)s);
		char ce[16], cj[16];

		if (!*en || !*ja)
			fprintf(stderr, "string %d is missing a language\n", s);
		CHECK(*en && *ja);
		CHECK(tr_max((enum str)s) > 0);

		/* Over its room in either language is a string cut on the panel. */
		if (fmt_width(en) > tr_max((enum str)s) || fmt_width(ja) > tr_max((enum str)s))
			fprintf(stderr, "string %d: %d / %d columns, room for %d\n  %s\n  %s\n",
			        s, fmt_width(en), fmt_width(ja), tr_max((enum str)s), en, ja);
		CHECK(fmt_width(en) <= tr_max((enum str)s));
		CHECK(fmt_width(ja) <= tr_max((enum str)s));

		/* The same arguments in the same order, or snprintf reads the
		 * wrong ones. */
		convs(en, ce, sizeof(ce));
		convs(ja, cj, sizeof(cj));
		if (strcmp(ce, cj) != 0)
			fprintf(stderr, "string %d: %%%s in English, %%%s in Japanese\n", s, ce, cj);
		CHECK_STR(cj, ce);
	}
}

/* The footer of a list of games: the hint with its verb, and the position
 * at the right, "999 / 9999", with a gap. 53 columns from column 1. */
static void test_footers(void)
{
	for (int l = 0; l < N_LANG; l++) {
		const enum str verbs[] = { S_FAVOURITE, S_REMOVE };
		for (int v = 0; v < 2; v++) {
			int w = fmt_width(tr_in((enum lang)l, S_HINT_GAMES)) +
			        text_width(tr_in((enum lang)l, verbs[v]));
			CHECK(1 + w + 2 + 10 + 1 <= 53);
		}
		const enum str bt[] = { S_BT_CHANGE, S_BT_DISCONNECT, S_BT_CONNECT };
		for (int v = 0; v < 3; v++)
			CHECK(fmt_width(tr_in((enum lang)l, S_HINT_BT)) +
			      text_width(tr_in((enum lang)l, bt[v])) <= 51);
	}
}

static void test_language(void)
{
	CHECK_INT(lang_parse("ja_JP"), LANG_JA);
	CHECK_INT(lang_parse("ja"), LANG_JA);
	CHECK_INT(lang_parse("en_US"), LANG_EN);
	CHECK_INT(lang_parse("jav"), LANG_EN);
	CHECK_INT(lang_parse(""), LANG_EN);
	CHECK_INT(lang_parse(NULL), LANG_EN);
	CHECK_STR(lang_value(LANG_JA), "ja_JP");
	CHECK_STR(lang_value(LANG_EN), "en_US");

	lang_set(LANG_JA);
	CHECK_STR(tr(S_SETTINGS), "\xe8\xa8\xad\xe5\xae\x9a");          /* 設定 */
	CHECK_STR(lang_system("n64", "Nintendo 64"), "NINTENDO64");
	CHECK_STR(lang_system("scummvm", "ScummVM"), "ScummVM");
	CHECK_STR(lang_system("xyz", ""), "xyz");
	CHECK_STR(lang_palette("grey"), "\xe3\x82\xb0\xe3\x83\xac\xe3\x83\xbc");   /* グレー */
	CHECK_STR(lang_palette("plum"), "plum");
	CHECK_STR(lang_usb_mode("network"),
	          "\xe3\x83\x8d\xe3\x83\x83\xe3\x83\x88\xe3\x83\xaf\xe3\x83\xbc\xe3\x82\xaf");

	/* Every Japanese console name fits the Systems row: from column 4,
	 * with a count of up to four digits right-aligned at 51. */
	const char *names[] = { "arcade", "atomiswave", "dreamcast", "famicom", "fds",
		"gamecube", "gamegear", "gb", "gba", "gbah", "gbav", "gbc", "gbch", "gbh",
		"genesis", "genh", "ggh", "imageviewer", "mastersystem", "megacd",
		"megadrive", "megadrive-japan", "megadriveh", "movies", "music", "n64",
		"n64dd", "neocd", "neogeo", "nes", "ports", "ps2", "ps3", "psp", "psx",
		"satellaview", "saturn", "sega32x", "segacd", "sfc", "snes", "snesh",
		"snesmsu1", "tools", "wiiware" };
	for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
		const char *ja = lang_system(names[i], "");
		CHECK(strcmp(ja, names[i]) != 0);        /* it has a Japanese name */
		CHECK(text_width(ja) <= 51 - 4 - 4 - 2);
	}

	lang_set(LANG_EN);
	CHECK_STR(tr(S_SETTINGS), "Settings");
	CHECK_STR(lang_system("n64", "Nintendo 64"), "Nintendo 64");
	CHECK_STR(lang_palette("grey"), "grey");
}

int main(void)
{
	test_table();
	test_footers();
	test_language();
	return check_report("lang");
}
