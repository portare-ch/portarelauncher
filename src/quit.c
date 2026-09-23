#include "quit.h"

#include <dirent.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void quit_reset(struct quit_combo *q)
{
	memset(q, 0, sizeof(*q));
}

int quit_feed(struct quit_combo *q, int dev, unsigned type, unsigned code, int value)
{
	if (dev < 0 || dev >= QUIT_MAX_DEV || type != QUIT_EV_KEY)
		return 0;
	if (value == 2)                 /* kernel autorepeat: not a new press */
		return 0;

	unsigned char *mine, *other;
	if (code == QUIT_BTN_MODE) {
		mine = &q->home[dev];
		other = &q->start[dev];
	} else if (code == QUIT_BTN_START) {
		mine = &q->start[dev];
		other = &q->home[dev];
	} else {
		return 0;
	}

	*mine = value != 0;
	return value == 1 && *other;
}

int quit_stat_pgrp(const char *stat, int *pgrp, int *ppid)
{
	const char *p = strrchr(stat, ')');
	if (!p)
		return -1;
	char state;
	int pp, pg;
	if (sscanf(p + 1, " %c %d %d", &state, &pp, &pg) != 3)
		return -1;
	*ppid = pp;
	*pgrp = pg;
	return 0;
}

int quit_signal_group(int pgrp, int keep, int sig)
{
	DIR *d = opendir("/proc");
	if (!d)
		return 0;

	int n = 0;
	struct dirent *e;
	while ((e = readdir(d))) {
		char *end;
		long pid = strtol(e->d_name, &end, 10);
		if (*end || pid <= 0 || pid == keep)
			continue;

		char path[64], line[512];
		snprintf(path, sizeof(path), "/proc/%ld/stat", pid);
		FILE *f = fopen(path, "r");
		if (!f)
			continue;
		int ok = fgets(line, sizeof(line), f) != NULL;
		fclose(f);

		int pg, pp;
		if (ok && quit_stat_pgrp(line, &pg, &pp) == 0 && pg == pgrp &&
		    kill((pid_t)pid, sig) == 0)
			n++;
	}
	closedir(d);
	return n;
}
