#include "check.h"
#include "glyph.h"

/* UTF-8 spelled out, so the file says exactly which bytes are tested. */
#define E_ACUTE      "\xc3\xa9"            /* é, precomposed      */
#define COMB_ACUTE   "\xcc\x81"            /* U+0301              */
#define KA           "\xe3\x82\xab"        /* カ                  */
#define DAKUTEN      "\xe3\x82\x99"        /* U+3099, combining   */
#define GA           "\xe3\x82\xac"        /* ガ                  */
#define S_COMMA      "\xc8\x99"            /* ș, U+0219           */
#define ROMAN_4      "\xe2\x85\xa3"        /* Ⅳ, U+2163           */
#define KIN          "\xe9\x87\x91"        /* 金                  */
#define PO_HANGUL    "\xed\x8f\xac"        /* 포, U+D3EC          */
#define MOTHER2      "MOTHER2 \xe3\x82\xae\xe3\x83\xbc\xe3\x82\xb0\xe3\x81\xae" \
                     "\xe9\x80\x86\xe8\xa5\xb2"   /* MOTHER2 ギーグの逆襲 */

static uint32_t first(const char *s)
{
	return text_next(&s);
}

static void test_decode(void)
{
	const char *s = "a" E_ACUTE;
	CHECK_INT(text_next(&s), 'a');
	CHECK_INT(text_next(&s), 0xE9);
	CHECK_INT(text_next(&s), 0);

	/* Decomposed, as a Mac names a file: one character. */
	s = "e" COMB_ACUTE "x";
	CHECK_INT(text_next(&s), 0xE9);
	CHECK_INT(text_next(&s), 'x');
	s = KA DAKUTEN;
	CHECK_INT(text_next(&s), 0x30AC);
	CHECK_INT(text_next(&s), 0);

	/* A mark that composes with nothing is dropped; one at the start
	 * belongs to nothing. */
	s = "q" COMB_ACUTE "z";
	CHECK_INT(text_next(&s), 'q');
	CHECK_INT(text_next(&s), 'z');
	s = COMB_ACUTE "a";
	CHECK_INT(text_next(&s), 'a');

	/* Not UTF-8: each byte is Latin-1. A cut sequence, a lone
	 * continuation byte, an overlong slash, a surrogate. */
	CHECK_INT(first("\xe9t\xe9"), 0xE9);
	CHECK_INT(first("\xe3\x82"), 0xE3);
	CHECK_INT(first("\x80"), 0x80);
	CHECK_INT(first("\xc0\xaf"), 0xC0);
	CHECK_INT(first("\xed\xa0\x80"), 0xED);
	CHECK_INT(first("\xf0\x9f\x98\x80"), 0x1F600);
}

static void test_glyphs(void)
{
	struct glyph g;

	g = glyph_of('A');
	CHECK_INT(g.code, 'A');
	CHECK_INT(g.width, 1);

	/* CP437 has é at 0x82, the box drawing at 0xC4, ○ at 0x09. */
	CHECK_INT(glyph_of(0xE9).code, 0x82);
	CHECK_INT(glyph_of(0x2500).code, 0xC4);
	CHECK_INT(glyph_of(0x25CB).code, 0x09);
	CHECK_INT(glyph_of(0xE9).width, 1);

	/* Kana and kanji: Unifont, two cells. */
	g = glyph_of(0x30AC);
	CHECK(g.code >= GLYPH_UNI);
	CHECK_INT(g.width, 2);
	CHECK_INT(glyph_of(0x91D1).width, 2);

	/* Ⅳ is in JIS X 0213 and Unifont draws it 8 wide: one cell. */
	g = glyph_of(0x2163);
	CHECK(g.code >= GLYPH_UNI);
	CHECK_INT(g.width, 1);

	/* ș is in neither font; its base letter is. */
	CHECK_INT(glyph_of(0x0219).code, 's');
	/* Hangul and an emoji: in neither, and no base. */
	CHECK_INT(glyph_of(0xD3EC).code, '?');
	CHECK_INT(glyph_of(0x1F600).code, '?');

	/* The bitmaps: a wide one has ink in its right half. */
	int wide;
	const unsigned char *b = glyph_bits(glyph_of(0x91D1).code, &wide);
	CHECK(b != NULL);
	CHECK_INT(wide, 1);
	int right = 0;
	for (int r = 0; r < 16; r++)
		right |= b[r * 2 + 1];
	CHECK(right != 0);
	CHECK(glyph_bits('A', &wide) == NULL);
}

static void test_width(void)
{
	CHECK_INT(text_width("Pok" E_ACUTE "mon"), 7);
	CHECK_INT(text_width("Poke" COMB_ACUTE "mon"), 7);
	CHECK_INT(text_width(KA DAKUTEN), 2);
	CHECK_INT(text_width(MOTHER2), 8 + 12);
	CHECK_INT(text_width("FF" ROMAN_4), 3);
	CHECK_INT(text_width(PO_HANGUL), 1);
	CHECK_INT(text_width(""), 0);

	/* Cutting never splits a character, and keeps a base with its mark. */
	CHECK_INT(text_fit(MOTHER2, 9), 8);         /* the kana would need 10 */
	CHECK_INT(text_fit(MOTHER2, 10), 8 + 3);
	CHECK_INT(text_fit(MOTHER2, 11), 8 + 3);
	CHECK_INT(text_fit("Poke" COMB_ACUTE "mon", 4), 4 + 2);
	CHECK_INT(text_fit("Poke" COMB_ACUTE "mon", 3), 3);
	CHECK_INT(text_fit("abc", 99), 3);
	CHECK_INT(text_fit(KIN, 1), 0);
}

/* The marquee as the launcher runs it: a column a tick, the view starting
 * at the character the column falls in. Over MOTHER2 ギーグの逆襲 in a
 * field 12 wide: ASCII leaves a column a tick, each kana holds two ticks,
 * and the scroll stops with the end in view. */
static void test_marquee(void)
{
	const char *s = MOTHER2;
	int end = text_scroll_end(s, 12);
	CHECK_INT(end, 8);                        /* 20 wide, 8 over */
	CHECK_INT(text_width(text_at_col(s, end)), 12);

	CHECK(text_at_col(s, 0) == s);
	CHECK(text_at_col(s, 1) == s + 1);       /* "OTHER2 ..." */
	CHECK(text_at_col(s, 8) == s + 8);       /* ギ, the first kana */
	CHECK(text_at_col(s, 9) == s + 8);       /* ギ still: its second tick */
	CHECK(text_at_col(s, 10) == s + 11);     /* ー */
	CHECK(text_at_col(s, 99) == s + strlen(s));

	/* A stop that falls inside a wide character moves to its end. */
	CHECK_INT(text_scroll_end(KIN KIN KIN, 5), 2);
	CHECK_INT(text_scroll_end(KIN "a" KIN, 4), 2);
	CHECK_INT(text_scroll_end("abc", 3), 0);
}

static void test_compose(void)
{
	char a[] = "Poke" COMB_ACUTE "mon";
	text_compose(a);
	CHECK_STR(a, "Pok" E_ACUTE "mon");

	char b[] = KA DAKUTEN "!";
	text_compose(b);
	CHECK_STR(b, GA "!");

	char c[] = "q" COMB_ACUTE "z";
	text_compose(c);
	CHECK_STR(c, "qz");

	/* Latin-1 stays as it was: rewriting it as UTF-8 would not fit, and
	 * it reads the same either way. */
	char d[] = "caf\xe9";
	text_compose(d);
	CHECK_STR(d, "caf\xe9");
	CHECK_INT(text_width(d), 4);

	char e[] = MOTHER2;
	text_compose(e);
	CHECK_STR(e, MOTHER2);
}

int main(void)
{
	test_decode();
	test_glyphs();
	test_width();
	test_marquee();
	test_compose();
	return check_report("glyph");
}
