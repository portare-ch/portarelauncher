/* Header status, refreshed on minute boundaries or FIFO notifications. */
#ifndef PL_STATUS_H
#define PL_STATUS_H

struct status {
	int capacity;    /* percent, or -1 when there is no battery */
	int charging;
	int volume;      /* percent, or -1 when unknown */
	int brightness;
	char clock[8];   /* "HH:MM" */
};

/* Volume and brightness use system.cfg values written by input_sense. */
void status_read(struct status *s);

/* Milliseconds until the next minute boundary. */
int  status_ms_to_next_minute(void);

#endif
