#include "check.h"
#include "fit.h"

static void check_fit(struct fit f, unsigned x, unsigned y, unsigned w,
                      unsigned h, unsigned zoom)
{
	CHECK_INT((int)f.x, (int)x);
	CHECK_INT((int)f.y, (int)y);
	CHECK_INT((int)f.w, (int)w);
	CHECK_INT((int)f.h, (int)h);
	CHECK_INT((int)f.zoom, (int)zoom);
}

static void test_panel(void)
{
	check_fit(fit_box(1280, 960, 1280, 960), 0, 0, 1280, 960, 1);
}

static void test_wide(void)
{
	/* 4K: twice the panel, bars on all four sides. */
	check_fit(fit_box(3840, 2160, 1280, 960), 640, 120, 2560, 1920, 2);
	check_fit(fit_box(1920, 1080, 1280, 960), 320, 60, 1280, 960, 1);
	/* 1440p is only one panel high. */
	check_fit(fit_box(2560, 1440, 1280, 960), 640, 240, 1280, 960, 1);
}

static void test_four_three(void)
{
	check_fit(fit_box(2560, 1920, 1280, 960), 0, 0, 2560, 1920, 2);
	check_fit(fit_box(1600, 1200, 1280, 960), 160, 120, 1280, 960, 1);
}

static void test_small(void)
{
	check_fit(fit_box(1280, 720, 1280, 960), 0, 0, 1280, 720, 0);
	check_fit(fit_box(1024, 768, 1280, 960), 0, 0, 1024, 768, 0);
	check_fit(fit_box(1280, 960, 0, 0), 0, 0, 1280, 960, 0);
}

int main(void)
{
	test_panel();
	test_wide();
	test_four_three();
	test_small();
	return check_report("fit");
}
