/* Display hotplug: the kernel's uevents for the DRM card, which arrive when
 * a monitor is plugged or pulled and when external-display switches the
 * panel's connector off or back. */
#ifndef PL_UEVENT_H
#define PL_UEVENT_H

#include <stddef.h>

/* A non-blocking netlink socket for kernel uevents, or -1. */
int uevent_open(void);

/* Reads everything waiting and returns 1 if any of it was a DRM hotplug. */
int uevent_read_hotplug(int fd);

/* Whether one uevent message (NUL-separated fields) is a DRM hotplug. */
int uevent_is_drm_hotplug(const char *msg, size_t len);

#endif
