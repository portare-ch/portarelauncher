#include "check.h"
#include "osinfo.h"

/* The device's own /etc/os-release. */
static const char *device =
	"OS_NAME=\"PortareOS\"\n"
	"OS_VERSION=\"20260923\"\n"
	"OS_BUILD=\"nightly\"\n"
	"GIT_ORGANIZATION=\"portare-ch\"\n"
	"BUILD_ID=\"2a9c631f84e644dd6bd6bdba5584c038bbade157\"\n"
	"BUILD_BRANCH=\"next\"\n"
	"BUILD_DATE=\"Wed Sep 23 00:53:32 UTC 2026\"\n"
	"HW_DEVICE=\"SM8550\"\n"
	"HW_ARCH=\"aarch64\"\n"
	"HW_CPU=\"Snapdragon 8 Gen2\"\n";

int main(void)
{
	char *dir = fixture_dir();
	char p[96];
	snprintf(p, sizeof(p), "%s/os-release", dir);
	struct osinfo o;

	fixture_write(p, device);
	CHECK_INT(osinfo_read(p, &o), 0);
	CHECK_STR(o.version, "20260923");
	CHECK_STR(o.build, "nightly");
	CHECK_STR(o.commit, "2a9c631f84e644dd6bd6bdba5584c038bbade157");
	CHECK_STR(o.branch, "next");
	CHECK_STR(o.date, "Wed Sep 23 00:53:32 UTC 2026");
	CHECK_STR(o.device, "SM8550");
	CHECK_STR(o.cpu, "Snapdragon 8 Gen2");

	/* Unquoted and single-quoted values are allowed too; a key that only
	 * starts the same way is not the key. */
	fixture_write(p, "OS_VERSION=20261001\nOS_BUILD='release'\nOS_BUILDER=x\n");
	CHECK_INT(osinfo_read(p, &o), 0);
	CHECK_STR(o.version, "20261001");
	CHECK_STR(o.build, "release");
	CHECK_STR(o.commit, "");

	CHECK_INT(osinfo_read("/tmp/pl-test-does-not-exist", &o), -1);
	CHECK_STR(o.version, "");

	fixture_cleanup(dir);
	return check_report("osinfo");
}
