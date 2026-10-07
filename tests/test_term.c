#include "check.h"
#include "term.h"

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

int main(void)
{
	test_marks();
	return check_report("term");
}
