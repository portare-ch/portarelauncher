#include "status.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>

#define PSY "/sys/class/power_supply"

static int read_line(const char *path, char *out, size_t osz)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return 0;
	int ok = fgets(out, (int)osz, f) != NULL;
	fclose(f);
	if (!ok)
		return 0;
	out[strcspn(out, "\n")] = '\0';
	return out[0] != '\0';
}

static int read_int(const char *path, int *out)
{
	char buf[32];
	if (!read_line(path, buf, sizeof(buf)))
		return 0;
	*out = atoi(buf);
	return 1;
}

/* A paired DualSense reports its own charge under this directory, as
 * ps-controller-battery-<mac> with type=Battery. Picking the first battery
 * would put the controller's percentage in the handheld's header, so the
 * built-in one is asked for by name and the scan that follows skips anything
 * that names itself a controller. */
static int find_battery(char *out, size_t osz)
{
	/* PSY plus a 255-byte d_name plus the leaf, with room to spare. */
	char path[512];

	snprintf(path, sizeof(path), PSY "/battery/capacity");
	if (access(path, R_OK) == 0) {
		snprintf(out, osz, PSY "/battery");
		return 1;
	}

	DIR *d = opendir(PSY);
	if (!d)
		return 0;

	int found = 0;
	struct dirent *e;
	while (!found && (e = readdir(d))) {
		if (e->d_name[0] == '.' || strstr(e->d_name, "controller"))
			continue;

		char type[32];
		snprintf(path, sizeof(path), PSY "/%s/type", e->d_name);
		if (!read_line(path, type, sizeof(type)) || strcmp(type, "Battery") != 0)
			continue;

		snprintf(path, sizeof(path), PSY "/%s/capacity", e->d_name);
		if (access(path, R_OK) != 0)
			continue;

		snprintf(out, osz, PSY "/%s", e->d_name);
		found = 1;
	}
	closedir(d);
	return found;
}

void status_read(struct status *s)
{
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	snprintf(s->clock, sizeof(s->clock), "%02d:%02d", tm.tm_hour, tm.tm_min);

	s->capacity = -1;
	s->charging = 0;

	char dir[512], path[544], state[32];
	if (!find_battery(dir, sizeof(dir)))
		return;

	snprintf(path, sizeof(path), "%s/capacity", dir);
	if (!read_int(path, &s->capacity))
		s->capacity = -1;

	snprintf(path, sizeof(path), "%s/status", dir);
	if (read_line(path, state, sizeof(state)))
		s->charging = strcmp(state, "Charging") == 0 ||
		              strcmp(state, "Full") == 0;
}

int status_ms_to_next_minute(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);

	time_t now = tv.tv_sec;
	struct tm tm;
	localtime_r(&now, &tm);

	int ms = (60 - tm.tm_sec) * 1000 - (int)(tv.tv_usec / 1000);
	if (ms < 250)
		ms = 250;      /* never spin if we land on the boundary */
	return ms;
}
