/* Copyright (C) 2026-present PortareOS */
#include "check.h"
#include "fake_proc.h"
#include "proc.h"
#include "sdcard.h"

#define CID_A "035344534e313238808f3e2a9d0161c3"
#define CID_B "275048534430384730000c8e5f00fd00"

static void test_status(void)
{
	struct sdcard c;

	fake_reset();
	fake_reply("/usr/bin/sdcard status",
	           "state=ready\nbytes=127865454592\nname=SN128\nid=" CID_A "\n", 0);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_READY);
	CHECK(c.bytes == 127865454592LL);
	CHECK_STR(c.name, "SN128");
	CHECK_STR(c.id, CID_A);
	CHECK_INT(sdcard_can_format(&c), 1);

	fake_reset();
	fake_reply("/usr/bin/sdcard status",
	           "state=format\nbytes=63864569856\nname=\nid=" CID_B "\n", 0);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_FORMAT);
	CHECK_STR(c.name, "");
	CHECK_INT(sdcard_can_format(&c), 1);

	/* A missing CID disables formatting. */
	fake_reset();
	fake_reply("/usr/bin/sdcard status", "state=format\nbytes=63864569856\n", 0);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_FORMAT);
	CHECK_INT(sdcard_can_format(&c), 0);

	/* Reject absent, system and undersized cards. */
	fake_reset();
	fake_reply("/usr/bin/sdcard status", "state=none\n", 0);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_NONE);
	CHECK_INT(sdcard_can_format(&c), 0);

	fake_reset();
	fake_reply("/usr/bin/sdcard status", "state=system\nbytes=63864569856\nname=SD64G\n", 0);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_SYSTEM);
	CHECK_INT(sdcard_can_format(&c), 0);

	fake_reset();
	fake_reply("/usr/bin/sdcard status", "state=small\nbytes=7948206080\nname=SU08G\n", 0);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_SMALL);
	CHECK_INT(sdcard_can_format(&c), 0);

	/* Unknown states disable formatting. */
	fake_reset();
	fake_reply("/usr/bin/sdcard status", "state=mystery\nbytes=1\n", 0);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_UNKNOWN);
	CHECK_INT(sdcard_can_format(&c), 0);
}

static void test_no_helper(void)
{
	struct sdcard c;

	/* Handle systems without the helper. */
	fake_reset();
	sdcard_read(&c);
	CHECK_INT(c.state, SD_UNKNOWN);
	CHECK(c.bytes == 0);
	CHECK_INT(sdcard_can_format(&c), 0);
	CHECK_INT(sdcard_format(CID_A), SDE_FAILED);

	/* Discard partial status after an error. */
	fake_reset();
	fake_reply("/usr/bin/sdcard status", "state=ready\nbytes=127865454592\n", 1);
	sdcard_read(&c);
	CHECK_INT(c.state, SD_UNKNOWN);
	CHECK(c.bytes == 0);
}

static void test_confirm(void)
{
	struct sdcard now = { .state = SD_READY, .bytes = 127865454592LL, .id = CID_A };
	CHECK_INT(sdcard_confirm(&now, CID_A), 1);

	/* Reject a different card. */
	struct sdcard swapped = { .state = SD_FORMAT, .bytes = 7948206080LL, .id = CID_B };
	CHECK_INT(sdcard_confirm(&swapped, CID_A), 0);

	/* Reject removed or ineligible cards. */
	struct sdcard none = { .state = SD_NONE };
	CHECK_INT(sdcard_confirm(&none, CID_A), 0);
	struct sdcard system = { .state = SD_SYSTEM, .id = CID_A };
	CHECK_INT(sdcard_confirm(&system, CID_A), 0);

	/* Empty IDs never match. */
	struct sdcard blank = { .state = SD_READY };
	CHECK_INT(sdcard_confirm(&blank, ""), 0);
}

static void test_format(void)
{
	/* Pass the confirmed CID to the helper. */
	fake_reset();
	fake_reply("/usr/bin/sdcard format " CID_A, "state=ready\n", 0);
	CHECK_INT(sdcard_format(CID_A), SDE_OK);
	CHECK_INT(fake_n_calls, 1);
	CHECK_STR(fake_calls[0], "/usr/bin/sdcard format " CID_A);

	const struct { const char *out; enum sd_error want; } cases[] = {
		{ "error=none\n", SDE_NONE }, { "error=system\n", SDE_SYSTEM },
		{ "error=small\n", SDE_SMALL }, { "error=busy\n", SDE_BUSY },
		{ "error=changed\n", SDE_CHANGED }, { "error=failed\n", SDE_FAILED },
		{ "error=mystery\n", SDE_FAILED },
	};
	for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		fake_reset();
		fake_reply("/usr/bin/sdcard format " CID_A, cases[i].out, 1);
		CHECK_INT(sdcard_format(CID_A), cases[i].want);
		CHECK_INT(fake_called("/usr/bin/sdcard restore"), 0);
	}

	/* Attempt mount recovery after an unexplained failure. */
	const int silent[] = { PROC_TIMEOUT, PROC_FAILED, 1 };
	for (size_t i = 0; i < sizeof(silent) / sizeof(silent[0]); i++) {
		fake_reset();
		fake_reply("/usr/bin/sdcard format " CID_A, "", silent[i]);
		fake_reply("/usr/bin/sdcard restore", "", 0);
		CHECK_INT(sdcard_format(CID_A), SDE_FAILED);
		CHECK_INT(fake_n_calls, 2);
		CHECK_STR(fake_calls[1], "/usr/bin/sdcard restore");
	}
}

static void test_size(void)
{
	char s[16];
	sdcard_size(127865454592LL, s, sizeof(s));
	CHECK_STR(s, "128 GB");
	sdcard_size(63864569856LL, s, sizeof(s));
	CHECK_STR(s, "64 GB");
	sdcard_size(31914983424LL, s, sizeof(s));
	CHECK_STR(s, "32 GB");
	sdcard_size(7948206080LL, s, sizeof(s));
	CHECK_STR(s, "7.9 GB");
	sdcard_size(1000204886016LL, s, sizeof(s));
	CHECK_STR(s, "1 TB");
	sdcard_size(1500301910016LL, s, sizeof(s));
	CHECK_STR(s, "1.5 TB");
}

int main(void)
{
	test_status();
	test_no_helper();
	test_confirm();
	test_format();
	test_size();
	return check_report("sdcard");
}
