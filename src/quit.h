/* Home + START exits any launched program: allow a grace period, then
 * SIGTERM, then SIGKILL. Signals target the child's process group except
 * runemu.sh, which must finish restoring performance settings. */
#ifndef PL_QUIT_H
#define PL_QUIT_H

/* Linux input constants for builds without linux/input.h. */
#define QUIT_EV_KEY     0x01
#define QUIT_BTN_START  0x13b
#define QUIT_BTN_MODE   0x13c     /* Home, Guide: the button in the middle */

#define QUIT_MAX_DEV    24

struct quit_combo {
	unsigned char home[QUIT_MAX_DEV];
	unsigned char start[QUIT_MAX_DEV];
};

void quit_reset(struct quit_combo *q);

/* Returns 1 when a press completes Home + START on the same device. */
int quit_feed(struct quit_combo *q, int dev, unsigned type, unsigned code, int value);

/* Parses pgrp and ppid from /proc/<pid>/stat. Counts fields from the last
 * ')' because command names may contain spaces and parentheses. Returns 0 or -1. */
int quit_stat_pgrp(const char *stat, int *pgrp, int *ppid);

/* Signals process group pgrp except keep; returns the number signalled. */
int quit_signal_group(int pgrp, int keep, int sig);

#endif
