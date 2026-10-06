#include "notify.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int notify_open(struct notify *o)
{
	memset(o, 0, sizeof(*o));
	o->fd = -1;

	if (mkfifo(NOTIFY_PATH, 0622) < 0 && errno != EEXIST) {
		fprintf(stderr, "mkfifo %s: %s\n", NOTIFY_PATH, strerror(errno));
		return -1;
	}
	/* mkfifo's mode goes through the umask, which on this system leaves
	 * 0600 and means only root could ever write. Set it for real, so a
	 * writer that is not root can still request a status refresh. */
	if (chmod(NOTIFY_PATH, 0622) < 0)
		fprintf(stderr, "chmod %s: %s\n", NOTIFY_PATH, strerror(errno));

	/* Read-write rather than read-only: a read-only FIFO with no writer
	 * reports EOF continuously and would spin the poll loop. */
	o->fd = open(NOTIFY_PATH, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (o->fd < 0) {
		fprintf(stderr, "open %s: %s\n", NOTIFY_PATH, strerror(errno));
		return -1;
	}
	return 0;
}

void notify_close(struct notify *o)
{
	if (o->fd >= 0)
		close(o->fd);
	o->fd = -1;
	unlink(NOTIFY_PATH);
}

void notify_drain(struct notify *n)
{
	char buf[256];
	ssize_t count;
	do {
		count = read(n->fd, buf, sizeof(buf));
	} while (count > 0 || (count < 0 && errno == EINTR));
}
