#include "check.h"
#include "modes.h"

#define N(a) ((int)(sizeof(a) / sizeof((a)[0])))

/* The LG UltraGear+ 4K over HBR2 x4, as msm listed it (4K 144 Hz pruned). */
static const struct mode_info lg[] = {
	{ 3840, 2160, 4000, 2222, 533250, 1, 0 },
	{ 3840, 2160, 4000, 2259, 858250, 0, 0 },
	{ 2560, 1440, 2720, 1543, 604250, 0, 0 },
	{ 1920, 1080, 2080, 1157, 346500, 0, 0 },
	{ 1920, 1080, 2200, 1125, 148500, 0, 0 },
	{ 1920, 1080, 2200, 1125, 148500, 0, 0 },
	{ 1920, 1080, 2200, 1125, 148352, 0, 0 },
	{ 1280, 1024, 1688, 1066, 135000, 0, 0 },
	{ 1280,  720, 1650,  750,  74250, 0, 0 },
	{ 1280,  720, 1650,  750,  74176, 0, 0 },
	{ 1024,  768, 1344,  806,  65000, 0, 0 },
	{  800,  600, 1056,  628,  40000, 0, 0 },
	{  720,  480,  858,  525,  27027, 0, 0 },
	{  720,  480,  858,  525,  27000, 0, 0 },
	{  640,  480,  800,  525,  25200, 0, 0 },
	{  640,  480,  800,  525,  25175, 0, 0 },
};

static void test_lg(void)
{
	/* 1080p at 143.98 Hz, not 4K or 1440p. */
	CHECK_INT(modes_pick(lg, N(lg), 0), 3);
	CHECK_INT((int)modes_refresh_mhz(&lg[3]), 143981);
	CHECK_INT((int)modes_refresh_mhz(&lg[2]), 143973);
}

/* 1920x1080 at r Hz with CVT-RB-like totals. */
#define FHD(r, pref) { 1920, 1080, 2080, 1111, 2080u * 1111u * (r) / 1000u, pref, 0 }

static void test_rates(void)
{
	/* 144 first, then 120, then the other rate nearest 120. */
	static const struct mode_info all[] = {
		FHD(60, 1), FHD(240, 0), FHD(120, 0), FHD(165, 0), FHD(144, 0),
	};
	CHECK_INT(modes_pick(all, N(all), 0), 4);
	static const struct mode_info no144[] = {
		FHD(60, 1), FHD(165, 0), FHD(120, 0),
	};
	CHECK_INT(modes_pick(no144, N(no144), 0), 2);
	static const struct mode_info fast[] = {
		FHD(60, 1), FHD(240, 0), FHD(165, 0),
	};
	CHECK_INT(modes_pick(fast, N(fast), 0), 2);
	/* A native 1440p monitor runs 1080p too. */
	static const struct mode_info qhd[] = {
		{ 2560, 1440, 2720, 1481, 241704, 1, 0 },
		{ 2560, 1440, 2720, 1543, 604250, 0, 0 },
		FHD(144, 0),
	};
	CHECK_INT(modes_pick(qhd, N(qhd), 0), 2);
}

static void test_panel(void)
{
	/* The panel: the fastest at its own size, whatever the rate. */
	static const struct mode_info panel[] = {
		{ 1280, 960, 1302, 998, 155772, 1, 0 },
		{ 1280, 960, 1302, 998, 156185, 0, 0 },
		{ 1280, 960, 1302, 1996, 155772, 0, 0 },
	};
	CHECK_INT(modes_pick(panel, N(panel), 1), 1);
}

static void test_aspects(void)
{
	/* A 4:3 monitor keeps 4:3 even where a larger 16:9 mode is faster. */
	static const struct mode_info crt[] = {
		{ 1600, 1200, 2160, 1250, 162000, 1, 0 },
		{ 1920, 1080, 2080, 1157, 346500, 0, 0 },
		{ 1024,  768, 1184,  813, 115500, 0, 0 },
		{  800,  600, 1056,  628,  80000, 0, 0 },
	};
	CHECK_INT(modes_pick(crt, N(crt), 0), 2);
	/* 16:10 likewise. */
	static const struct mode_info wide[] = {
		{ 1920, 1200, 2080, 1235, 154000, 1, 0 },
		{ 1920, 1080, 2080, 1157, 346500, 0, 0 },
		{ 1680, 1050, 1840, 1080, 238464, 0, 0 },
	};
	CHECK_INT(modes_pick(wide, N(wide), 0), 2);
}

static void test_fallbacks(void)
{
	/* Nothing 120 Hz class: the preferred mode. 100 Hz does not count. */
	static const struct mode_info slow[] = {
		{ 1920, 1080, 2200, 1125, 148500, 0, 0 },
		{ 2560, 1440, 2720, 1481, 241704, 1, 0 },
		{ 2560, 1440, 2720, 1481, 402840, 0, 0 },
	};
	CHECK_INT(modes_pick(slow, N(slow), 0), 1);
	/* 119.88 Hz counts; an interlaced mode never does. */
	static const struct mode_info ntsc[] = {
		{ 1920, 1080, 2200, 1125, 148500, 1, 0 },
		{ 1920, 1080, 2200, 1125, 296703, 0, 0 },
		{ 2560, 1440, 2720, 1543, 604250, 0, 1 },
	};
	CHECK_INT(modes_pick(ntsc, N(ntsc), 0), 1);
	CHECK_INT(modes_pick(ntsc, 0, 0), -1);
	/* Without a preferred mode, the first stands in for it. */
	static const struct mode_info none[] = {
		{ 1920, 1080, 2200, 1125, 148500, 0, 0 },
	};
	CHECK_INT(modes_pick(none, N(none), 0), 0);
}

int main(void)
{
	test_lg();
	test_rates();
	test_panel();
	test_aspects();
	test_fallbacks();
	return check_report("modes");
}
