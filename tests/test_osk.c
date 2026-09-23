#include "check.h"
#include "osk.h"

static void press(struct osk *k, enum action a, int times)
{
	while (times-- > 0)
		osk_action(k, a);
}

/* Every printable ASCII character can be typed, because a WPA passphrase
 * may use any of them. Found by searching the keyboard the way a thumb
 * would: every state reachable with the d-pad, shift and the layer key,
 * and what CONFIRM types from each. */
static void test_reach(void)
{
	enum { MAXQ = 4096 };
	static struct osk queue[MAXQ];
	static unsigned char seen[2][3][5][10];
	int reached[128] = { 0 };
	int head = 0, tail = 0;

	osk_init(&queue[tail++], 0);
	const enum action moves[] = { ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
	                              ACT_ALT, ACT_CONFIRM, ACT_MENU };

	while (head < tail) {
		struct osk cur = queue[head++];
		for (size_t i = 0; i < sizeof(moves) / sizeof(moves[0]); i++) {
			struct osk next = cur;
			osk_action(&next, moves[i]);
			if (next.len > cur.len) {
				reached[(unsigned char)next.text[next.len - 1]] = 1;
				continue;               /* typed: not a new place */
			}
			if (next.len != cur.len)
				continue;
			unsigned char *s = &seen[next.layer][next.shift][next.row][next.col];
			if (*s || tail >= MAXQ)
				continue;
			*s = 1;
			queue[tail++] = next;
		}
	}

	int missing = 0;
	for (int c = 32; c < 127; c++) {
		if (!reached[c]) {
			fprintf(stderr, "cannot type '%c' (%d)\n", c, c);
			missing++;
		}
	}
	CHECK_INT(missing, 0);
	for (int c = 0; c < 32; c++)
		CHECK(!reached[c]);
}

static void test_shift(void)
{
	struct osk k;
	osk_init(&k, 0);                    /* starts on 'q' */
	osk_action(&k, ACT_CONFIRM);
	CHECK_STR(k.text, "q");

	/* Once: one capital, then back to lower case. */
	osk_action(&k, ACT_ALT);
	press(&k, ACT_CONFIRM, 2);
	CHECK_STR(k.text, "qQq");

	/* A digit does not use up a single shift. */
	osk_init(&k, 0);
	osk_action(&k, ACT_ALT);
	osk_action(&k, ACT_UP);
	osk_action(&k, ACT_CONFIRM);        /* '1' */
	osk_action(&k, ACT_DOWN);
	press(&k, ACT_CONFIRM, 2);
	CHECK_STR(k.text, "1Qq");

	/* Twice: locked, until a third press. */
	osk_init(&k, 0);
	press(&k, ACT_ALT, 2);
	press(&k, ACT_CONFIRM, 2);
	osk_action(&k, ACT_ALT);
	osk_action(&k, ACT_CONFIRM);
	CHECK_STR(k.text, "QQq");
}

static void test_back_and_done(void)
{
	struct osk k;
	osk_init(&k, 8);
	press(&k, ACT_CONFIRM, 3);
	CHECK_INT(osk_action(&k, ACT_START), OSK_NONE);   /* 3 < 8 */
	press(&k, ACT_CONFIRM, 5);
	CHECK_INT(osk_action(&k, ACT_START), OSK_DONE);

	/* Back deletes, and leaves only from an empty field. */
	press(&k, ACT_BACK, 8);
	CHECK_INT(k.len, 0);
	CHECK_INT(osk_action(&k, ACT_BACK), OSK_CANCEL);

	/* An open network: done with nothing typed. */
	osk_init(&k, 0);
	CHECK_INT(osk_action(&k, ACT_START), OSK_DONE);
}

static void test_limits(void)
{
	struct osk k;
	osk_init(&k, 0);
	press(&k, ACT_CONFIRM, OSK_MAX + 10);
	CHECK_INT(k.len, OSK_MAX);
	CHECK_INT(strlen(k.text), OSK_MAX);

	osk_action(&k, ACT_SELECT);
	CHECK(k.hidden);
	osk_action(&k, ACT_SELECT);
	CHECK(!k.hidden);
}

static void test_wrap(void)
{
	/* Right from the last key of a row wraps to its first, and up from the
	 * digits reaches the bottom row. */
	struct osk k;
	osk_init(&k, 0);
	press(&k, ACT_LEFT, 1);             /* q -> p */
	osk_action(&k, ACT_CONFIRM);
	CHECK_STR(k.text, "p");
	osk_action(&k, ACT_RIGHT);          /* p -> q */
	osk_action(&k, ACT_CONFIRM);
	CHECK_STR(k.text, "pq");
	press(&k, ACT_UP, 2);
	CHECK_INT(k.row, 4);
}

int main(void)
{
	test_reach();
	test_shift();
	test_back_and_done();
	test_limits();
	test_wrap();
	return check_report("osk");
}
