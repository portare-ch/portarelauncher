#include "osd.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

long long osd_now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int osd_open(struct osd *o)
{
	memset(o, 0, sizeof(*o));
	o->fd = -1;

	if (mkfifo(OSD_PATH, 0622) < 0 && errno != EEXIST) {
		fprintf(stderr, "mkfifo %s: %s\n", OSD_PATH, strerror(errno));
		return -1;
	}

	/* Read-write rather than read-only: a read-only FIFO with no writer
	 * reports EOF continuously and would spin the poll loop. */
	o->fd = open(OSD_PATH, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	if (o->fd < 0) {
		fprintf(stderr, "open %s: %s\n", OSD_PATH, strerror(errno));
		return -1;
	}
	return 0;
}

void osd_close(struct osd *o)
{
	if (o->fd >= 0)
		close(o->fd);
	o->fd = -1;
	unlink(OSD_PATH);
}

void osd_read(struct osd *o)
{
	char buf[256];
	ssize_t n = read(o->fd, buf, sizeof(buf) - 1);
	if (n <= 0)
		return;
	buf[n] = '\0';

	/* Only the last line matters: holding a volume key writes faster than
	 * the overlay is worth redrawing, and the newest value is the one to
	 * show. */
	char *line = buf;
	for (char *p = buf; *p; p++) {
		if (*p == '\n') {
			*p = '\0';
			if (p[1])
				line = p + 1;
		}
	}
	if (!*line)
		return;

	/* Clipped rather than rejected: a message too long for the overlay is
	 * still better shown short than not at all. */
	size_t len = strlen(line);
	if (len >= sizeof(o->text))
		len = sizeof(o->text) - 1;
	memcpy(o->text, line, len);
	o->text[len] = '\0';
	o->until = osd_now_ms() + OSD_HOLD_MS;
}

int osd_remaining(struct osd *o)
{
	if (!o->until)
		return -1;

	long long left = o->until - osd_now_ms();
	if (left <= 0) {
		o->until = 0;
		o->text[0] = '\0';
		return -1;
	}
	return (int)left;
}
