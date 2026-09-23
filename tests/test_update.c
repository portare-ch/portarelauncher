#include "check.h"
#include "fake_proc.h"
#include "update.h"

#define CHECK_CMD "/usr/bin/portareos-update check-online"
#define FETCH_CMD "/usr/bin/portareos-update update-online"

/* What the script printed on the device against the real releases, with a
 * newer tag for the "update" case. */
static void test_check(void)
{
	struct update_info u;

	fake_reset();
	fake_reply(CHECK_CMD,
		"channel=nightly\n"
		"installed=20260923 2a9c631\n"
		"tag=nightly-20260924-115\n"
		"size=838594560\n"
		"state=update\n", 0);
	update_check(&u);
	CHECK_INT(u.state, UPD_AVAILABLE);
	CHECK_STR(u.channel, "nightly");
	CHECK_STR(u.installed, "20260923 2a9c631");
	CHECK_STR(u.tag, "nightly-20260924-115");
	CHECK_INT(u.size, 838594560LL);

	fake_reset();
	fake_reply(CHECK_CMD,
		"channel=nightly\ninstalled=20260923 2a9c631\n"
		"tag=nightly-20260923-111\nsize=838594560\nstate=current\n", 0);
	update_check(&u);
	CHECK_INT(u.state, UPD_CURRENT);

	fake_reset();
	fake_reply(CHECK_CMD,
		"channel=release\ninstalled=20260923 2a9c631\nstate=none\n", 0);
	update_check(&u);
	CHECK_INT(u.state, UPD_NONE);
	CHECK_STR(u.channel, "release");
	CHECK_STR(u.tag, "");

	fake_reset();
	fake_reply(CHECK_CMD,
		"channel=release\ninstalled=20260923 2a9c631\n"
		"tag=release-20260901-60\nsize=1\nstate=newer\n", 0);
	update_check(&u);
	CHECK_INT(u.state, UPD_NEWER);
}

static void test_check_failures(void)
{
	struct update_info u;

	/* No network: the script says why and exits 2. */
	fake_reset();
	fake_reply(CHECK_CMD,
		"channel=nightly\ninstalled=20260923 2a9c631\n"
		"error=Could not reach GitHub. Is Wi-Fi connected?\n", 2);
	update_check(&u);
	CHECK_INT(u.state, UPD_ERROR);
	CHECK_STR(u.error, "Could not reach GitHub. Is Wi-Fi connected?");
	CHECK_STR(u.channel, "nightly");        /* still shown */

	/* No script at all. */
	fake_reset();
	update_check(&u);
	CHECK_INT(u.state, UPD_ERROR);
	CHECK(u.error[0] != '\0');

	/* Exit 0 but no state line: not an answer. */
	fake_reset();
	fake_reply(CHECK_CMD, "channel=nightly\n", 0);
	update_check(&u);
	CHECK_INT(u.state, UPD_ERROR);

	/* Other output is ignored, not misread. */
	fake_reset();
	fake_reply(CHECK_CMD,
		"curl: (6) Could not resolve host\nstatex=update\nstate=current\n", 0);
	update_check(&u);
	CHECK_INT(u.state, UPD_CURRENT);
}

static int seen[8], n_seen, verified;

static void on_progress(int pct, int verifying, void *ctx)
{
	(void)ctx;
	if (verifying)
		verified = 1;
	else if (n_seen < 8)
		seen[n_seen++] = pct;
}

static void test_fetch(void)
{
	char err[96] = "";

	fake_reset();
	n_seen = verified = 0;
	fake_reply(FETCH_CMD,
		"progress=0\nprogress=42\nprogress=250\nprogress=100\n"
		"verifying=1\nstaged=PortareOS-SM8550.aarch64-20260924.tar\n", 0);
	CHECK_INT(update_fetch(on_progress, NULL, err, sizeof(err)), 0);
	CHECK_INT(n_seen, 4);
	CHECK_INT(seen[1], 42);
	CHECK_INT(seen[2], 100);                /* clamped */
	CHECK(verified);

	/* The script's reason is what the user sees. */
	fake_reset();
	fake_reply(FETCH_CMD,
		"progress=0\nprogress=75\n"
		"error=The download stopped. Try again to continue it.\n", 1);
	CHECK_INT(update_fetch(on_progress, NULL, err, sizeof(err)), -1);
	CHECK_STR(err, "The download stopped. Try again to continue it.");

	/* "staged" is not enough on its own: the script has to finish too. */
	fake_reset();
	fake_reply(FETCH_CMD, "staged=x.tar\n", 1);
	CHECK_INT(update_fetch(NULL, NULL, err, sizeof(err)), -1);
	CHECK(err[0] != '\0');

	/* Nor is a clean exit without it. */
	fake_reset();
	fake_reply(FETCH_CMD, "progress=100\n", 0);
	CHECK_INT(update_fetch(NULL, NULL, err, sizeof(err)), -1);
}

static void test_staged(void)
{
	char *dir = fixture_dir();
	char p[96];

	CHECK_INT(update_staged(dir), 0);

	/* A download in progress is not staged. */
	snprintf(p, sizeof(p), "%s/PortareOS-SM8550.aarch64-20260924.tar.part", dir);
	fixture_write(p, "");
	CHECK_INT(update_staged(dir), 0);

	snprintf(p, sizeof(p), "%s/PortareOS-SM8550.aarch64-20260924.tar", dir);
	fixture_write(p, "");
	CHECK_INT(update_staged(dir), 1);

	CHECK_INT(update_staged("/tmp/pl-test-does-not-exist"), 0);
	fixture_cleanup(dir);
}

static void test_channel(void)
{
	char *dir = fixture_dir();
	char p[96];
	snprintf(p, sizeof(p), "%s/system.cfg", dir);
	fixture_write(p, "updates.branch=auto\nupdates.enabled=1\n");

	CHECK_INT(update_set_channel(p, "release"), 0);
	CHECK_STR(fixture_read(p), "updates.branch=release\nupdates.enabled=1\n");
	fixture_cleanup(dir);
}

int main(void)
{
	test_check();
	test_check_failures();
	test_fetch();
	test_staged();
	test_channel();
	return check_report("update");
}
