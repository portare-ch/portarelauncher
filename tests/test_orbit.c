#include "check.h"
#include "orbit.h"

static void test_walk(void)
{
	/* Over one period every offset in -12..12 comes up, one pixel a minute,
	 * and the walk ends where it began. */
	int seen[25] = { 0 }, dx, dy, last_x = 0, steps_ok = 1, sum = 0;
	for (long long m = 1000; m <= 1048; m++) {
		orbit_offset(m, 12, 3, &dx, &dy);
		CHECK(dx >= -12 && dx <= 12);
		CHECK(dy >= -3 && dy <= 3);
		if (m > 1000 && (dx - last_x > 1 || last_x - dx > 1))
			steps_ok = 0;
		if (m < 1048) {
			seen[dx + 12]++;
			sum += dx;
		}
		last_x = dx;
	}
	CHECK(steps_ok);
	CHECK_INT(sum, 0);
	for (int i = 0; i < 25; i++)
		CHECK(seen[i] > 0);
	/* The ends get one minute a period, the rest two. */
	CHECK_INT(seen[0], 1);
	CHECK_INT(seen[24], 1);
	CHECK_INT(seen[12], 2);
}

static void test_axes(void)
{
	int dx, dy;
	/* Each axis on its own period: 48 minutes across, 12 down. */
	orbit_offset(0, 12, 3, &dx, &dy);
	CHECK_INT(dx, 0);
	CHECK_INT(dy, 0);
	orbit_offset(3, 12, 3, &dx, &dy);
	CHECK_INT(dx, 3);
	CHECK_INT(dy, 3);
	orbit_offset(9, 12, 3, &dx, &dy);
	CHECK_INT(dx, 9);
	CHECK_INT(dy, -3);
	orbit_offset(36, 12, 3, &dx, &dy);
	CHECK_INT(dx, -12);
	CHECK_INT(dy, 0);
}

static void test_edges(void)
{
	int dx, dy;
	/* No room, no walk. */
	orbit_offset(12345, 0, 0, &dx, &dy);
	CHECK_INT(dx, 0);
	CHECK_INT(dy, 0);
	/* A clock before the epoch walks the same way. */
	orbit_offset(-1, 12, 3, &dx, &dy);
	CHECK_INT(dx, -1);
	CHECK_INT(dy, -1);
}

int main(void)
{
	test_walk();
	test_axes();
	test_edges();
	return check_report("orbit");
}
