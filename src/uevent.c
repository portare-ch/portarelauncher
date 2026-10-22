#include "uevent.h"

#include <string.h>

int uevent_is_drm_hotplug(const char *msg, size_t len)
{
	int drm = 0, hotplug = 0;
	for (size_t i = 0; i < len; ) {
		const char *f = msg + i;
		size_t n = strnlen(f, len - i);
		if (n == 13 && !memcmp(f, "SUBSYSTEM=drm", 13))
			drm = 1;
		else if (n == 9 && !memcmp(f, "HOTPLUG=1", 9))
			hotplug = 1;
		i += n + 1;
	}
	return drm && hotplug;
}

/* The parser above is what the host tests cover; netlink is Linux only. */
#ifdef __linux__
#include <errno.h>
#include <linux/netlink.h>
#include <sys/socket.h>
#include <unistd.h>

int uevent_open(void)
{
	int fd = socket(AF_NETLINK, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
	                NETLINK_KOBJECT_UEVENT);
	if (fd < 0)
		return -1;
	/* Group 1 is the kernel's own broadcast; udev re-sends on group 2. */
	struct sockaddr_nl sa = { .nl_family = AF_NETLINK, .nl_groups = 1 };
	if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}

int uevent_read_hotplug(int fd)
{
	char buf[4096];
	int hit = 0;
	ssize_t n;
	for (;;) {
		n = recv(fd, buf, sizeof(buf), 0);
		if (n > 0)
			hit |= uevent_is_drm_hotplug(buf, (size_t)n);
		else if (n < 0 && errno == ENOBUFS)
			hit = 1;          /* some were dropped: one may have been ours */
		else
			break;
	}
	return hit;
}
#endif
