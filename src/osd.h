/* A one-line overlay for volume and brightness.
 *
 * This lives here because it has to. Only the process holding DRM master can
 * put anything on the panel, and in the menu that is this program. The
 * notification daemon that used to do it needed a compositor.
 *
 * It is a FIFO rather than a signal because the message has content, and
 * because a FIFO sits in the same poll() the input devices do: nothing wakes
 * while nobody is writing to it.
 *
 * The limit worth stating plainly: while a game is running this program has
 * dropped DRM master, so it cannot draw and the pipe just fills and is
 * drained later. An in-game OSD would have to come from the emulator, which
 * is the only thing holding the panel then.
 */
#ifndef PL_OSD_H
#define PL_OSD_H

#define OSD_PATH "/run/portarelauncher.osd"
#define OSD_HOLD_MS 1400

struct osd {
	int fd;
	char text[64];
	long long until;   /* monotonic ms, 0 when nothing is showing */
};

/* Creates the FIFO and opens it read-write, so it never reports EOF when no
 * writer happens to be attached. */
int  osd_open(struct osd *o);
void osd_close(struct osd *o);

/* Drains the pipe and starts the hold timer. */
void osd_read(struct osd *o);

/* Milliseconds until the overlay should come down, or -1 if nothing is
 * showing. Clears the text when the time has passed. */
int  osd_remaining(struct osd *o);

long long osd_now_ms(void);

#endif
