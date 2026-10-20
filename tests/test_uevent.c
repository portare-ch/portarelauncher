#include "check.h"
#include "uevent.h"

#define MSG(s) s, sizeof(s)

static void test_hotplug(void)
{
	/* What the msm card sends for a DP plug and a forced panel. */
	CHECK(uevent_is_drm_hotplug(MSG(
		"change@/devices/platform/soc@0/ae00000.display-subsystem/ae01000.display-controller/drm/card0\0"
		"ACTION=change\0"
		"DEVPATH=/devices/platform/soc@0/ae00000.display-subsystem/ae01000.display-controller/drm/card0\0"
		"SUBSYSTEM=drm\0"
		"HOTPLUG=1\0"
		"DEVNAME=dri/card0\0"
		"DEVTYPE=drm_minor\0"
		"SEQNUM=4242\0"
		"MAJOR=226\0"
		"MINOR=0")));
}

static void test_other(void)
{
	/* Not a hotplug: a DRM uevent without HOTPLUG, another subsystem. */
	CHECK(!uevent_is_drm_hotplug(MSG(
		"add@/devices/virtual/drm/ttm\0ACTION=add\0SUBSYSTEM=drm\0SEQNUM=1")));
	CHECK(!uevent_is_drm_hotplug(MSG(
		"change@/devices/x\0ACTION=change\0SUBSYSTEM=power_supply\0HOTPLUG=1")));
	/* Prefixes do not count. */
	CHECK(!uevent_is_drm_hotplug(MSG(
		"change@/x\0SUBSYSTEM=drm_dp_aux_dev\0HOTPLUG=1")));
	CHECK(!uevent_is_drm_hotplug(MSG(
		"change@/x\0SUBSYSTEM=drm\0HOTPLUG=10")));
	/* A truncated message is read to its end and no further. */
	CHECK(!uevent_is_drm_hotplug("SUBSYSTEM=drm\0HOTPLUG=1", 15));
	CHECK(!uevent_is_drm_hotplug("", 0));
}

int main(void)
{
	test_hotplug();
	test_other();
	return check_report("uevent");
}
