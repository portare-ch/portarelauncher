#include "check.h"
#include "term.h"
#include "glyph.h"

/* Three cells by two at scale 3: 72 x 96 pixels. */
#define W 72
#define H 96
static uint32_t fb[W * H];

static int lit_in(int x0, int y0, int w, int h, uint32_t c)
{
	int n = 0;
	for (int y = y0; y < y0 + h; y++)
		for (int x = x0; x < x0 + w; x++)
			n += fb[y * W + x] == c;
	return n;
}

static void test_marks(void)
{
	struct term t;
	memset(&t, 0, sizeof(t));
	CHECK_INT(term_init(&t, fb, W, W, H, 3), 0);
	term_set_palette(&t, 0);
	const uint32_t text = term_color(&t, ATTR_TEXT);
	const uint32_t bright = term_color(&t, ATTR_BRIGHT);

	static const unsigned char marks[4] = { G_MARK_CROSS, G_MARK_SQUARE,
	                                        G_MARK_TRIANGLE, G_MARK_CIRCLE };
	for (int i = 0; i < 4; i++) {
		memset(fb, 0, sizeof(fb));
		term_clear(&t);
		term_invalidate(&t);
		term_putc(&t, 1, 0, marks[i], ATTR_TEXT);
		term_flush(&t);

		/* Drawn, in the cell's colour, and only inside its cell. */
		int in = lit_in(24, 0, 24, 48, text);
		CHECK(in > 30);
		CHECK_INT(lit_in(0, 0, 24, 96, text) + lit_in(48, 0, 24, 96, text) +
		          lit_in(24, 48, 24, 48, text), 0);
		/* Not the font's glyph for that code: an outline, so the
		 * middle of three of the four is empty. The cross crosses
		 * its own middle. */
		if (marks[i] != G_MARK_CROSS)
			CHECK(fb[21 * W + 36] != text);

		/* A colour change redraws it in the new colour, nothing
		 * of the old left. */
		term_putc(&t, 1, 0, marks[i], ATTR_BRIGHT);
		term_flush(&t);
		CHECK_INT(lit_in(24, 0, 24, 48, text), 0);
		CHECK_INT(lit_in(24, 0, 24, 48, bright), in);

		/* And a space over it leaves the cell black. */
		term_putc(&t, 1, 0, ' ', ATTR_TEXT);
		term_flush(&t);
		CHECK_INT(lit_in(24, 0, 24, 48, bright), 0);
	}

	/* The codes around them are still the font's: the left arrow and
	 * the selection caret. */
	memset(fb, 0, sizeof(fb));
	term_clear(&t);
	term_invalidate(&t);
	term_putc(&t, 0, 1, 0x1B, ATTR_TEXT);
	term_putc(&t, 1, 1, G_CARET, ATTR_TEXT);
	term_flush(&t);
	CHECK(lit_in(0, 48, 24, 48, text) > 0);
	CHECK(lit_in(24, 48, 24, 48, text) > 0);
	/* Font pixels come in 3 x 3 blocks at scale 3; a two-pixel outline
	 * cannot. The caret's top-left lit pixel starts a full block. */
	int found = 0;
	for (int y = 48; y < 96 && !found; y++)
		for (int x = 24; x < 48 && !found; x++)
			if (fb[y * W + x] == text) {
				found = 1;
				CHECK(fb[(y + 2) * W + x + 2] == text);
			}
	CHECK(found);

	term_free(&t);
}

/* Text: ten cells by one row, 240 x 48 at scale 3. */
#define TW 240
#define TH 48
static uint32_t tfb[TW * TH];

static const struct cell *at(const struct term *t, unsigned x)
{
	return &t->cur[x];
}

#define KIN "\xe9\x87\x91"                 /* 金, two cells */
#define GA  "\xe3\x82\xab\xe3\x82\x99"     /* カ + U+3099: ガ */

static void test_text(void)
{
	struct term t;
	memset(&t, 0, sizeof(t));
	CHECK_INT(term_init(&t, tfb, TW, TW, TH, 3), 0);
	CHECK_INT(t.cols, 10);

	/* Mixed ASCII and Japanese: the kanji takes two cells, halves
	 * marked, and the text after it carries on from the third. */
	CHECK_INT(term_puts(&t, 0, 0, "a" KIN "b", ATTR_TEXT), 4);
	CHECK_INT(at(&t, 0)->g, 'a');
	CHECK(at(&t, 1)->g >= GLYPH_UNI);
	CHECK_INT(at(&t, 1)->half, HALF_LEFT);
	CHECK_INT(at(&t, 2)->half, HALF_RIGHT);
	CHECK_INT(at(&t, 2)->g, at(&t, 1)->g);
	CHECK_INT(at(&t, 3)->g, 'b');

	/* Writing over either half clears the other. */
	term_putc(&t, 2, 0, 'x', ATTR_TEXT);
	CHECK_INT(at(&t, 1)->g, ' ');
	CHECK_INT(at(&t, 1)->half, HALF_NONE);
	CHECK_INT(at(&t, 2)->g, 'x');
	term_puts(&t, 1, 0, KIN, ATTR_TEXT);
	term_putc(&t, 1, 0, 'y', ATTR_TEXT);
	CHECK_INT(at(&t, 2)->g, ' ');
	CHECK_INT(at(&t, 2)->half, HALF_NONE);
	/* A wide glyph over the right half of another clears that one's left. */
	term_clear(&t);
	term_puts(&t, 0, 0, KIN, ATTR_TEXT);
	term_puts(&t, 1, 0, KIN, ATTR_TEXT);
	CHECK_INT(at(&t, 0)->g, ' ');
	CHECK_INT(at(&t, 1)->half, HALF_LEFT);
	CHECK_INT(at(&t, 2)->half, HALF_RIGHT);

	/* At the right edge a kanji that would need two cells is not drawn
	 * by half: the line stops. */
	term_clear(&t);
	CHECK_INT(term_puts(&t, 7, 0, "ab" KIN, ATTR_TEXT), 2);
	CHECK_INT(at(&t, 9)->g, ' ');
	CHECK_INT(at(&t, 9)->half, HALF_NONE);

	/* Right-aligned by width: two kanji end on the last column. */
	term_clear(&t);
	term_puts_right(&t, 10, 0, KIN KIN, ATTR_TEXT);
	CHECK_INT(at(&t, 5)->g, ' ');
	CHECK_INT(at(&t, 6)->half, HALF_LEFT);
	CHECK_INT(at(&t, 9)->half, HALF_RIGHT);
	/* Next to Japanese text on its left, which it does not disturb. */
	term_puts(&t, 0, 0, KIN KIN, ATTR_TEXT);
	CHECK_INT(at(&t, 3)->half, HALF_RIGHT);
	CHECK_INT(at(&t, 6)->half, HALF_LEFT);

	/* Decomposed ガ is one character, two cells, and it is ガ that is
	 * drawn, not カ with its mark dropped. */
	term_clear(&t);
	CHECK_INT(term_puts(&t, 0, 0, GA, ATTR_TEXT), 2);
	CHECK_INT(at(&t, 0)->g, glyph_of(0x30AC).code);

	/* Flushed, the wide glyph is drawn across both cells: ink in each. */
	memset(tfb, 0, sizeof(tfb));
	term_clear(&t);
	term_invalidate(&t);
	term_puts(&t, 0, 0, KIN, ATTR_TEXT);
	term_flush(&t);
	const uint32_t text = term_color(&t, ATTR_TEXT);
	int left = 0, right = 0;
	for (int y = 0; y < TH; y++)
		for (int x = 0; x < 48; x++)
			if (tfb[y * TW + x] == text) {
				if (x < 24)
					left++;
				else
					right++;
			}
	CHECK(left > 0 && right > 0);
	/* Overwritten on its right half, the whole glyph goes. */
	term_putc(&t, 1, 0, ' ', ATTR_TEXT);
	term_flush(&t);
	int ink = 0;
	for (int y = 0; y < TH; y++)
		for (int x = 0; x < 48; x++)
			ink += tfb[y * TW + x] == text;
	CHECK_INT(ink, 0);

	term_free(&t);
}

int main(void)
{
	test_marks();
	test_text();
	return check_report("term");
}
