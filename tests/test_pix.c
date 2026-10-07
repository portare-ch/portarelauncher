#include "check.h"
#include "pix.h"

#define W 48
#define H 48
static uint32_t fb[H * W];
static struct pix p = { fb, W, W, H };

static int at(int x, int y) { return fb[y * W + x] != 0; }
static void clear(void) { memset(fb, 0, sizeof(fb)); }

static int count(void)
{
	int n = 0;
	for (int i = 0; i < W * H; i++)
		n += fb[i] != 0;
	return n;
}

/* The same set of pixels mirrored left to right. Round shapes are centred
 * on a pixel, cx; lines on pixel edges, so a line-drawn mark centred on cx
 * is symmetric about the edge left of it, and pixel x mirrors to
 * 2 cx - 1 - x. */
static int mirrored(int cx, int edge)
{
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			int m = 2 * cx - x - edge;
			if (m < 0 || m >= W)
				continue;
			if (at(x, y) != at(m, y))
				return 0;
		}
	return 1;
}

static void test_ring_disc(void)
{
	clear();
	pix_ring(&p, 24, 24, 10, 2, 1);
	CHECK(at(24, 15));             /* in the band, top    */
	CHECK(at(15, 24));             /* in the band, left   */
	CHECK(!at(24, 24));            /* the middle is empty */
	CHECK(!at(24, 17));            /* inside the band     */
	CHECK(!at(24, 13));            /* outside the ring    */
	CHECK(mirrored(24, 0));

	clear();
	pix_disc(&p, 24, 24, 4, 1);
	CHECK(at(24, 24));
	CHECK(at(21, 24));
	CHECK(!at(20, 24));
	CHECK(mirrored(24, 0));
}

static void test_line(void)
{
	/* Two pixels thick means two rows, not one or three. */
	clear();
	pix_line(&p, 5, 30, 25, 30, 2, 1);
	CHECK(at(10, 29) && at(10, 30));
	CHECK(!at(10, 28) && !at(10, 31));

	/* A diagonal as thick as a straight line, measured across it. */
	clear();
	pix_line(&p, 10, 10, 30, 30, 2, 1);
	CHECK(at(20, 20));
	CHECK(!at(24, 18));
	int n = count();
	CHECK(n > 40 && n < 80);

	/* A point is a dot. */
	clear();
	pix_line(&p, 20, 20, 20, 20, 2, 1);
	CHECK(at(20, 20) || at(19, 19));
}

static void test_marks(void)
{
	/* Outlines, never filled: the centre stays empty. */
	clear();
	pix_square(&p, 24, 24, 11, 2, 1);
	CHECK(!at(24, 24));
	CHECK(count() > 0);
	CHECK(mirrored(24, 1));

	clear();
	pix_circle(&p, 24, 24, 11, 2, 1);
	CHECK(!at(24, 24));
	CHECK(mirrored(24, 0));

	clear();
	pix_triangle(&p, 24, 24, 11, 2, 1);
	CHECK(!at(24, 24));
	CHECK(at(24, 13) || at(23, 13));      /* the apex */
	CHECK(mirrored(24, 1));

	/* The cross is the one mark that crosses its own centre. */
	clear();
	pix_cross(&p, 24, 24, 11, 2, 1);
	CHECK(at(24, 24) || at(23, 23));
	CHECK(mirrored(24, 1));
}

static void test_clip(void)
{
	/* Off every edge, and nothing is written outside the buffer: the
	 * guard words around it stay as they were. */
	static uint32_t big[(H + 2) * W];
	struct pix q = { big + W, W, W, H };
	for (int i = 0; i < (H + 2) * W; i++)
		big[i] = 0;
	pix_ring(&q, -5, -5, 20, 2, 1);
	pix_disc(&q, W + 3, H + 3, 10, 1);
	pix_line(&q, -100, 10, 200, 10, 4, 1);
	pix_fill(&q, -10, H - 2, W + 20, 10, 1);
	int outside = 0;
	for (int i = 0; i < W; i++)
		outside += big[i] != 0 || big[(H + 1) * W + i] != 0;
	CHECK_INT(outside, 0);
	CHECK(big[W + 10 * W + 0] != 0);     /* the line reached the edge */
}

int main(void)
{
	test_ring_disc();
	test_line();
	test_marks();
	test_clip();
	return check_report("pix");
}
