/* Clock and battery for the header.
 *
 * Both are read from sysfs and the system clock on a timer rather than
 * continuously. The launcher otherwise blocks forever in poll(), so the
 * wake-up interval is the idle cost of this program: it is the time until
 * the next minute boundary, which is sixty wake-ups an hour to read two
 * small files and repaint a handful of cells.
 */
#ifndef PL_STATUS_H
#define PL_STATUS_H

struct status {
	int capacity;    /* percent, or -1 when there is no battery */
	int charging;
	int volume;      /* percent, or -1 when unknown */
	int brightness;
	char clock[8];   /* "HH:MM" */
};

/* Volume and brightness come from system.cfg rather than from pipewire and
 * sysfs, because that is where input_sense puts them and where every other
 * setting on this device lives. One source, already authoritative. */
void status_read(struct status *s);

/* Milliseconds until the next minute ticks over, so the clock is never more
 * than a second late and never wakes more often than it has to. */
int  status_ms_to_next_minute(void);

#endif
