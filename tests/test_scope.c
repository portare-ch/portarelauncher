#include "check.h"
#include "scope.h"

#define KEY(c, v) scope_feed(&s, SC_EV_KEY, (c), (v), 0)
#define ABS(c, v) scope_feed(&s, SC_EV_ABS, (c), (v), 0)
#define NEAR(a, b) ((a) - (b) < 0.001 && (b) - (a) < 0.001)

static struct scope s;

static void test_virtual_map(void)
{
	scope_reset(&s, SCOPE_VIRTUAL);

	/* InputPlumber's pad names the face buttons by position. */
	CHECK_INT(KEY(SC_BTN_NORTH, 1), 1);
	CHECK(scope_lit(&s, CTL_TOP));
	CHECK(!scope_lit(&s, CTL_WEST));
	KEY(SC_BTN_NORTH, 0);
	CHECK(!scope_lit(&s, CTL_TOP));
	KEY(SC_BTN_WEST, 1);
	CHECK(scope_lit(&s, CTL_WEST));
	KEY(SC_BTN_SOUTH, 1);
	KEY(SC_BTN_EAST, 1);
	CHECK(scope_lit(&s, CTL_BOTTOM));
	CHECK(scope_lit(&s, CTL_EAST));

	/* The paddles are the extra triggers there, and BTN_Z means nothing. */
	KEY(SC_BTN_TRIGGER_HAPPY3, 1);
	KEY(SC_BTN_TRIGGER_HAPPY4, 1);
	CHECK(scope_lit(&s, CTL_M1));
	CHECK(scope_lit(&s, CTL_M2));
	KEY(SC_BTN_TRIGGER_HAPPY3, 0);
	KEY(SC_BTN_TRIGGER_HAPPY4, 0);
	KEY(SC_BTN_Z, 1);
	CHECK(!scope_lit(&s, CTL_M1));

	/* The middle row and the stick clicks. */
	KEY(SC_BTN_SELECT, 1);
	KEY(SC_BTN_MODE, 1);
	KEY(SC_BTN_START, 1);
	KEY(SC_BTN_THUMBL, 1);
	KEY(SC_BTN_THUMBR, 1);
	CHECK(scope_lit(&s, CTL_SELECT) && scope_lit(&s, CTL_HOME) &&
	      scope_lit(&s, CTL_START));
	CHECK(scope_lit(&s, CTL_L3) && scope_lit(&s, CTL_R3));

	/* Autorepeat is not a change; a code nothing maps still is news. */
	CHECK_INT(KEY(SC_BTN_START, 2), 0);
	CHECK(scope_lit(&s, CTL_START));
	CHECK_INT(KEY(0x2c5, 1), 1);
}

static void test_raw_map(void)
{
	scope_reset(&s, SCOPE_RAW);

	/* The MCU has top and left the other way round (nova_mcu.yaml). */
	KEY(SC_BTN_WEST, 1);
	CHECK(scope_lit(&s, CTL_TOP));
	CHECK(!scope_lit(&s, CTL_WEST));
	KEY(SC_BTN_WEST, 0);
	KEY(SC_BTN_NORTH, 1);
	CHECK(scope_lit(&s, CTL_WEST));
	CHECK(!scope_lit(&s, CTL_TOP));

	/* The paddles are gpio-keys: BTN_Z left, BTN_C right. */
	KEY(SC_BTN_Z, 1);
	CHECK(scope_lit(&s, CTL_M1));
	CHECK(!scope_lit(&s, CTL_M2));
	KEY(SC_BTN_C, 1);
	CHECK(scope_lit(&s, CTL_M2));
	KEY(SC_BTN_TRIGGER_HAPPY3, 1);
	KEY(SC_BTN_Z, 0);
	CHECK(!scope_lit(&s, CTL_M1));

	/* The D-pad as buttons, which is how the MCU sends it. */
	KEY(SC_BTN_DPAD_LEFT, 1);
	CHECK(scope_lit(&s, CTL_LEFT));
	KEY(SC_BTN_DPAD_LEFT, 0);
	CHECK(!scope_lit(&s, CTL_LEFT));

	/* A layer switch forgets what the other layer held. */
	scope_reset(&s, SCOPE_VIRTUAL);
	CHECK(!scope_lit(&s, CTL_M2));
}

static void test_hat(void)
{
	scope_reset(&s, SCOPE_VIRTUAL);
	ABS(SC_ABS_HAT0X, -1);
	CHECK(scope_lit(&s, CTL_LEFT) && !scope_lit(&s, CTL_RIGHT));
	ABS(SC_ABS_HAT0X, 1);
	CHECK(!scope_lit(&s, CTL_LEFT) && scope_lit(&s, CTL_RIGHT));
	ABS(SC_ABS_HAT0X, 0);
	CHECK(!scope_lit(&s, CTL_LEFT) && !scope_lit(&s, CTL_RIGHT));
	ABS(SC_ABS_HAT0Y, -1);
	CHECK(scope_lit(&s, CTL_UP));
	ABS(SC_ABS_HAT0Y, 1);
	CHECK(scope_lit(&s, CTL_DOWN) && !scope_lit(&s, CTL_UP));
}

static void test_axes(void)
{
	scope_reset(&s, SCOPE_VIRTUAL);

	/* 0..255 at rest on 128 is a hair right of centre, not a bias. */
	CHECK(scope_axis(&s, AX_LX) > 0 && scope_axis(&s, AX_LX) < 0.005);
	ABS(SC_ABS_X, 0);
	CHECK(NEAR(scope_axis(&s, AX_LX), -1));
	ABS(SC_ABS_X, 255);
	CHECK(NEAR(scope_axis(&s, AX_LX), 1));

	/* The MCU's own range, and where the stick is when it is read. */
	scope_range(&s, SC_ABS_RY, -1024, 1024, -512);
	CHECK(NEAR(scope_axis(&s, AX_RY), -0.5));
	ABS(SC_ABS_RY, 2000);                 /* past the end is the end */
	CHECK(NEAR(scope_axis(&s, AX_RY), 1));

	/* Triggers run from 0, and lit means past rest. */
	scope_range(&s, SC_ABS_Z, 0, 1830, 0);
	CHECK(NEAR(scope_axis(&s, AX_LT), 0));
	CHECK(!scope_lit(&s, CTL_L2));
	ABS(SC_ABS_Z, 915);
	CHECK(NEAR(scope_axis(&s, AX_LT), 0.5));
	CHECK(scope_lit(&s, CTL_L2));
	ABS(SC_ABS_Z, 0);
	CHECK(!scope_lit(&s, CTL_L2));
	KEY(SC_BTN_TL2, 1);                   /* the digital half lights it too */
	CHECK(scope_lit(&s, CTL_L2));

	/* A range that is not one is ignored rather than divided by. */
	scope_range(&s, SC_ABS_RX, 5, 5, 5);
	CHECK(NEAR(scope_axis(&s, AX_RX), (128 - 127.5) / 127.5));
}

static void test_rate(void)
{
	scope_reset(&s, SCOPE_RAW);
	CHECK_INT(scope_rate(&s, 0), 0);

	/* 200 Hz for a second. */
	for (int i = 0; i < 200; i++)
		scope_feed(&s, SC_EV_SYN, SC_SYN_REPORT, 0, 1000 + i * 5);
	CHECK_INT(scope_rate(&s, 1995), 200);
	/* Half a second on, half of them are still inside the window. */
	CHECK_INT(scope_rate(&s, 2495), 100);
	/* Still for a second: nothing. */
	CHECK_INT(scope_rate(&s, 2995), 0);

	/* Only SYN_REPORT counts; SYN_DROPPED is not a report. */
	scope_feed(&s, SC_EV_SYN, 3, 0, 3000);
	CHECK_INT(scope_rate(&s, 3000), 0);

	/* Two reports a millisecond, more in a second than the ring holds:
	 * the count stops at the ring rather than reading stale slots. */
	for (int i = 0; i < 4 * SCOPE_RATE_N; i++)
		scope_feed(&s, SC_EV_SYN, SC_SYN_REPORT, 0, 10000 + i / 2);
	CHECK_INT(scope_rate(&s, 10000 + 2 * SCOPE_RATE_N), SCOPE_RATE_N);
}

static void test_last(void)
{
	scope_reset(&s, SCOPE_VIRTUAL);
	CHECK_INT(s.nlast, 0);

	ABS(SC_ABS_X, 10);
	ABS(SC_ABS_Y, 20);
	CHECK_INT(s.nlast, 2);
	CHECK_INT(s.last[0].code, SC_ABS_Y);
	CHECK_INT(s.last[1].code, SC_ABS_X);

	/* A stick moving sends X and Y in turn; the rows stay put and the
	 * values follow, the latest on top. */
	ABS(SC_ABS_X, 11);
	CHECK_INT(s.last[0].code, SC_ABS_X);
	CHECK_INT(s.last[0].value, 11);
	CHECK_INT(s.last[1].code, SC_ABS_Y);
	ABS(SC_ABS_X, 12);
	CHECK_INT(s.last[0].value, 12);
	CHECK_INT(s.last[1].code, SC_ABS_Y);

	/* Something else pushes the oldest out. */
	KEY(SC_BTN_SOUTH, 1);
	CHECK_INT(s.nlast, 2);
	CHECK_INT(s.last[0].code, SC_BTN_SOUTH);
	CHECK_INT(s.last[0].type, SC_EV_KEY);
	CHECK_INT(s.last[1].code, SC_ABS_X);

	/* SYN is not input. */
	scope_feed(&s, SC_EV_SYN, SC_SYN_REPORT, 0, 0);
	CHECK_INT(s.last[0].code, SC_BTN_SOUTH);
}

static void test_names(void)
{
	char b[24];
	CHECK_STR(scope_code_name(SC_EV_ABS, SC_ABS_RY, b, sizeof(b)), "ABS_RY");
	CHECK_STR(scope_code_name(SC_EV_KEY, SC_BTN_SOUTH, b, sizeof(b)), "BTN_SOUTH");
	CHECK_STR(scope_code_name(SC_EV_KEY, SC_BTN_TRIGGER_HAPPY3, b, sizeof(b)),
	          "BTN_HAPPY3");
	CHECK_STR(scope_code_name(SC_EV_KEY, 0x2c5, b, sizeof(b)), "KEY 0x2c5");
	CHECK_STR(scope_code_name(SC_EV_ABS, 0x28, b, sizeof(b)), "ABS 0x028");
}

int main(void)
{
	test_virtual_map();
	test_raw_map();
	test_hat();
	test_axes();
	test_rate();
	test_last();
	test_names();
	return check_report("scope");
}
