#include "check.h"
#include "color.h"

static void test_fixed(void)
{
	CHECK(color_ctm_fixed(1.0) == (1ull << 32));
	CHECK(color_ctm_fixed(0.0) == 0);
	CHECK(color_ctm_fixed(0.5) == (1ull << 31));
	/* Sign-magnitude: the sign bit and the magnitude, not two's
	 * complement. The kernel reads it as CONVERT_S3_15(). */
	CHECK(color_ctm_fixed(-0.5) == ((1ull << 63) | (1ull << 31)));
	CHECK(color_ctm_fixed(0.7919) == 3401184602ull);
}

static void write_file(const char *path, int lut_lines, const char *extra)
{
	FILE *f = fopen(path, "w");
	fprintf(f, "# a test profile\n%s", extra);
	fprintf(f, "ctm 0.79 0.12 0.03  0.06 0.89 0.03  0.04 0.08 0.87\n");
	for (int i = 0; i < lut_lines; i++)
		fprintf(f, "lut %d %d %d\n", i * 64, i * 64 + 1, 65535);
	fclose(f);
}

static void test_load(void)
{
	char path[] = "/tmp/pl_color_XXXXXX";
	close(mkstemp(path));
	struct color_profile p;

	write_file(path, 1024, "");
	CHECK_INT(color_load(path, &p), 0);
	CHECK(p.has_ctm && p.has_lut);
	CHECK(p.ctm[0] == 0.79 && p.ctm[4] == 0.89 && p.ctm[8] == 0.87);
	CHECK_INT(p.lut[0][0], 0);
	CHECK_INT(p.lut[1023][0], 1023 * 64);
	CHECK_INT(p.lut[5][1], 5 * 64 + 1);
	CHECK_INT(p.lut[7][2], 65535);

	/* A short table is refused, not padded. */
	write_file(path, 1000, "");
	CHECK_INT(color_load(path, &p), -1);

	/* A matrix alone is fine: the gamma block stays off. */
	write_file(path, 0, "");
	CHECK_INT(color_load(path, &p), 0);
	CHECK(p.has_ctm && !p.has_lut);

	/* Junk is an error with the line named, not silently skipped. */
	write_file(path, 1024, "gamma 2.2\n");
	CHECK_INT(color_load(path, &p), -1);

	unlink(path);
	CHECK_INT(color_load(path, &p), -1);
}

int main(void)
{
	test_fixed();
	test_load();
	return check_report("color");
}
