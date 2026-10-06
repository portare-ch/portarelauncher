/* FIFO notifications that prompt a refresh of the header status.
 * The path is retained for compatibility with input_sense's writer.
 */
#ifndef PL_NOTIFY_H
#define PL_NOTIFY_H

#define NOTIFY_PATH "/run/portarelauncher.osd"

struct notify {
	int fd;
};

/* Opens read-write so an absent writer never causes repeated EOF wake-ups. */
int notify_open(struct notify *n);
void notify_close(struct notify *n);

/* Discards pending bytes; the authoritative values come from system.cfg. */
void notify_drain(struct notify *n);

#endif
