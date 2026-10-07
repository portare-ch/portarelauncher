#include "check.h"
#include "ja26_fixture.h"

static unsigned char g[3][JA26_BYTES];

static void test_load(void)
{
	char *dir = fixture_dir(), path[64];
	snprintf(path, sizeof(path), "%s/ja26.bin", dir);
	const uint32_t cps[3] = { 0x3042, 0x91D1, 0x91D2 };
	memset(g[0], 0x11, JA26_BYTES);
	memset(g[1], 0x22, JA26_BYTES);

	CHECK(ja26_find(0x3042) == NULL);          /* nothing loaded yet */
	CHECK_INT(ja26_load("/nonexistent/ja26.bin"), -1);

	ja26_fixture(path, "PLJA26\0\1", 2, JA26_SIZE, cps, g, 0);
	CHECK_INT(ja26_load(path), 0);
	CHECK(ja26_find(0x3042) && ja26_find(0x3042)[0] == 0x11);
	CHECK(ja26_find(0x91D1) && ja26_find(0x91D1)[JA26_BYTES - 1] == 0x22);
	CHECK(ja26_find(0x3043) == NULL);
	CHECK(ja26_find(0) == NULL);

	/* Anything that is not this format is refused, and what was loaded
	 * stays: a bad file never takes the glyphs away. */
	ja26_fixture(path, "PLJA27\0\1", 2, JA26_SIZE, cps, g, 0);
	CHECK_INT(ja26_load(path), -1);
	ja26_fixture(path, "PLJA26\0\1", 2, 24, cps, g, 0);
	CHECK_INT(ja26_load(path), -1);
	ja26_fixture(path, "PLJA26\0\1", 2, JA26_SIZE, cps, g, 1);
	CHECK_INT(ja26_load(path), -1);
	/* Says 3, holds 2: a glyph's code point and bitmap missing. */
	ja26_fixture(path, "PLJA26\0\1", 3, JA26_SIZE, cps, g, 4 + JA26_BYTES);
	CHECK_INT(ja26_load(path), -1);
	CHECK(ja26_find(0x3042) && ja26_find(0x3042)[0] == 0x11);

	fixture_cleanup(dir);
}

int main(void)
{
	test_load();
	return check_report("ja26");
}
