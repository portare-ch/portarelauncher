#include "osk.h"

#include <ctype.h>
#include <string.h>

/* CP437, which is all the VGA font has. */
#define G_BOX_TL   0xDA
#define G_BOX_TR   0xBF
#define G_BOX_BL   0xC0
#define G_BOX_BR   0xD9
#define G_BOX_V    0xB3
#define G_BULLET   0x07
#define G_MORE     0x11   /* left-pointing triangle: text runs off the left */

#define ROWS       4      /* character rows; the bottom row is ROWS itself */
#define COLS       10
#define CELL       5      /* columns per key: a 120 px target on the panel */

/* The digits sit on top of both layers so they are always one press up.
 * - _ . @ live on the letter layer because they turn up in passwords far
 * more than the rest; the other 28 symbols fill the second layer, which is
 * why its last row ends two cells early. Between them: all 95 printable
 * ASCII characters, each exactly once (space is on the bottom row). */
static const char layers[2][ROWS][COLS + 1] = {
	{ "1234567890", "qwertyuiop", "asdfghjkl-", "zxcvbnm_.@" },
	{ "1234567890", "!\"#$%&'()*", "+,/:;<=>?[", "\\]^`{|}~" },
};

/* The bottom row is the same ten cells spent unevenly: the keys you reach
 * for without looking are the wide ones. */
enum { BK_SHIFT, BK_LAYER, BK_SPACE, BK_DEL, BK_DONE, N_BK };
static const struct { int col, width; } bottom[N_BK] = {
	{ 0, 2 }, { 2, 2 }, { 4, 3 }, { 7, 1 }, { 8, 2 },
};

static int bottom_key(int col)
{
	for (int i = N_BK - 1; i >= 0; i--)
		if (col >= bottom[i].col)
			return i;
	return 0;
}

static char cell_char(const struct osk *k, int row, int col)
{
	char c = layers[k->layer][row][col];
	if (c && k->layer == 0 && k->shift && islower((unsigned char)c))
		c = (char)toupper((unsigned char)c);
	return c;
}

static int valid(const struct osk *k, int row, int col)
{
	return row == ROWS || layers[k->layer][row][col] != '\0';
}

/* After a move, or a layer change, may have left focus on one of the two
 * empty cells at the end of the symbols layer. Step left until it is not. */
static void settle(struct osk *k)
{
	while (!valid(k, k->row, k->col))
		k->col--;
	if (k->row == ROWS)
		k->col = bottom[bottom_key(k->col)].col;
}

void osk_init(struct osk *k, int min_len)
{
	memset(k, 0, sizeof(*k));
	k->min_len = min_len;
	k->row = 1;                 /* q: where a hand starts on a keyboard */
}

static void type(struct osk *k, char c)
{
	if (!c || k->len >= OSK_MAX)
		return;
	k->text[k->len++] = c;
	k->text[k->len] = '\0';
	/* Shift once is for one letter, as on a phone. A digit typed while it
	 * is set leaves it set, because the digit did not use it. */
	if (k->shift == 1 && isalpha((unsigned char)c))
		k->shift = 0;
}

static void backspace(struct osk *k)
{
	if (k->len > 0)
		k->text[--k->len] = '\0';
}

static enum osk_result done(const struct osk *k)
{
	return k->len >= k->min_len ? OSK_DONE : OSK_NONE;
}

static void cycle_shift(struct osk *k)
{
	k->shift = (k->shift + 1) % 3;      /* off, next letter, locked */
}

static enum osk_result press(struct osk *k)
{
	if (k->row < ROWS) {
		type(k, cell_char(k, k->row, k->col));
		return OSK_NONE;
	}
	switch (bottom_key(k->col)) {
	case BK_SHIFT: cycle_shift(k); break;
	case BK_LAYER: k->layer ^= 1; break;
	case BK_SPACE: type(k, ' '); break;
	case BK_DEL:   backspace(k); break;
	case BK_DONE:  return done(k);
	}
	return OSK_NONE;
}

static void move_h(struct osk *k, int dir)
{
	if (k->row == ROWS) {
		int i = (bottom_key(k->col) + dir + N_BK) % N_BK;
		k->col = bottom[i].col;
		return;
	}
	/* Wrap, skipping the empty cells: right from '~' lands on '\'. */
	do {
		k->col = (k->col + dir + COLS) % COLS;
	} while (!valid(k, k->row, k->col));
}

static void move_v(struct osk *k, int dir)
{
	/* Wraps too, so DONE is one press up from the digits. */
	k->row = (k->row + dir + ROWS + 1) % (ROWS + 1);
	settle(k);
}

enum osk_result osk_action(struct osk *k, enum action a)
{
	switch (a) {
	case ACT_UP:      move_v(k, -1); break;
	case ACT_DOWN:    move_v(k, +1); break;
	case ACT_LEFT:    move_h(k, -1); break;
	case ACT_RIGHT:   move_h(k, +1); break;
	case ACT_CONFIRM: return press(k);
	case ACT_BACK:
		if (k->len == 0)
			return OSK_CANCEL;
		backspace(k);
		break;
	case ACT_MENU:    type(k, ' '); break;
	case ACT_ALT:     cycle_shift(k); break;
	case ACT_START:   return done(k);
	case ACT_SELECT:  k->hidden = !k->hidden; break;
	default:          break;
	}
	return OSK_NONE;
}

/* ---- drawing ------------------------------------------------------------ */

static void draw_field(const struct osk *k, struct term *t, unsigned y)
{
	unsigned x0 = 2, w = t->cols - 4;          /* border to border */
	unsigned inner = w - 4;                    /* margin inside each side */

	term_putc(t, x0, y, G_BOX_TL, ATTR_DIM);
	term_putc(t, x0 + w - 1, y, G_BOX_TR, ATTR_DIM);
	term_putc(t, x0, y + 2, G_BOX_BL, ATTR_DIM);
	term_putc(t, x0 + w - 1, y + 2, G_BOX_BR, ATTR_DIM);
	for (unsigned x = x0 + 1; x < x0 + w - 1; x++) {
		term_putc(t, x, y, G_HLINE, ATTR_DIM);
		term_putc(t, x, y + 2, G_HLINE, ATTR_DIM);
	}
	term_putc(t, x0, y + 1, G_BOX_V, ATTR_DIM);
	term_putc(t, x0 + w - 1, y + 1, G_BOX_V, ATTR_DIM);

	/* The text plus a cursor. Sixty-three characters do not fit in the
	 * box, so it shows the end - the part being typed - behind a marker
	 * that says there is more to the left. */
	unsigned total = (unsigned)k->len + 1;
	unsigned first = 0, x = x0 + 2;
	if (total > inner) {
		term_putc(t, x++, y + 1, G_MORE, ATTR_MID);
		first = total - (inner - 1);
	}
	for (unsigned i = first; i < (unsigned)k->len; i++)
		term_putc(t, x++, y + 1,
		          k->hidden ? G_BULLET : (unsigned char)k->text[i], ATTR_TEXT);
	term_putc(t, x, y + 1, '_', ATTR_BRIGHT);
}

static void draw_key(struct term *t, unsigned x, unsigned y, unsigned width,
                     const char *label, int selected, int attr)
{
	unsigned w = CELL * width, len = (unsigned)strlen(label);
	unsigned lx = x + (w - len) / 2;

	if (selected) {
		term_putc(t, x, y, '[', ATTR_BRIGHT);
		term_putc(t, x + w - 1, y, ']', ATTR_BRIGHT);
		attr = ATTR_BRIGHT;
	}
	for (unsigned i = 0; i < len; i++)
		term_putc(t, lx + i, y, (unsigned char)label[i], attr);
}

unsigned osk_draw(const struct osk *k, struct term *t, unsigned y)
{
	unsigned left = (t->cols - CELL * COLS) / 2;

	draw_field(k, t, y);
	y += 4;

	for (int r = 0; r < ROWS; r++) {
		for (int c = 0; c < COLS; c++) {
			char ch = cell_char(k, r, c);
			if (!ch)
				continue;
			char label[2] = { ch, '\0' };
			draw_key(t, left + (unsigned)(CELL * c), y + (unsigned)r, 1, label,
			         k->row == r && k->col == c, ATTR_TEXT);
		}
	}

	static const char *const shift_label[3] = { "SHIFT", "SHIFT", "CAPS" };
	unsigned by = y + ROWS;
	int sel = k->row == ROWS ? bottom_key(k->col) : -1;
	for (int i = 0; i < N_BK; i++) {
		const char *label = "";
		int attr = ATTR_MID;
		switch (i) {
		case BK_SHIFT:
			label = shift_label[k->shift];
			if (k->shift)
				attr = ATTR_TEXT;      /* lit while it applies */
			break;
		case BK_LAYER: label = k->layer ? "abc" : "#+="; break;
		case BK_SPACE: label = "SPACE"; break;
		case BK_DEL:   label = "DEL"; break;
		case BK_DONE:
			label = "DONE";
			if (k->len < k->min_len)
				attr = ATTR_DIM;       /* not yet: too short */
			break;
		}
		draw_key(t, left + (unsigned)(CELL * bottom[i].col), by,
		         (unsigned)bottom[i].width, label, i == sel, attr);
	}
	return by + 1;
}
