#include "update.h"
#include "proc.h"
#include "settings.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UPDATE_SCRIPT "/usr/bin/portareos-update"

/* The script limits curl to 30 seconds; allow extra time for script overhead. */
#define CHECK_MS 60000

/* Allow slow downloads; the script separately aborts minute-long stalls. */
#define FETCH_MS (90 * 60 * 1000)

/* Return the value for an exact key=value prefix, or NULL. */
static char *kv(char *line, const char *key)
{
	size_t n = strlen(key);
	if (strncmp(line, key, n) != 0 || line[n] != '=')
		return NULL;
	return line + n + 1;
}

static void cb_check(char *line, void *ctx)
{
	struct update_info *u = ctx;
	char *v;

	if ((v = kv(line, "channel")))
		str_copy(u->channel, sizeof(u->channel), v);
	else if ((v = kv(line, "installed")))
		str_copy(u->installed, sizeof(u->installed), v);
	else if ((v = kv(line, "tag")))
		str_copy(u->tag, sizeof(u->tag), v);
	else if ((v = kv(line, "size")))
		u->size = atoll(v);
	else if ((v = kv(line, "error"))) {
		str_copy(u->error, sizeof(u->error), v);
		u->state = UPD_ERROR;
	} else if ((v = kv(line, "state"))) {
		if (!strcmp(v, "update"))       u->state = UPD_AVAILABLE;
		else if (!strcmp(v, "current")) u->state = UPD_CURRENT;
		else if (!strcmp(v, "newer"))   u->state = UPD_NEWER;
		else if (!strcmp(v, "none"))    u->state = UPD_NONE;
	}
}

void update_check(struct update_info *u)
{
	memset(u, 0, sizeof(*u));
	char *const argv[] = { (char *)UPDATE_SCRIPT, (char *)"check-online", NULL };
	int rc = proc_run_for(argv, cb_check, u, CHECK_MS);

	if (u->state == UPD_ERROR)
		return;
	if (rc != 0 || u->state == UPD_UNKNOWN) {
		u->state = UPD_ERROR;
		if (rc == PROC_TIMEOUT)
			str_copy(u->error, sizeof(u->error), "No answer from GitHub.");
		else
			snprintf(u->error, sizeof(u->error),
			         "Update check failed (%d).", rc);
	}
}

struct fetch_ctx {
	void (*progress)(int pct, int verifying, void *ctx);
	void *ctx;
	char error[96];
	int staged;
};

static void cb_fetch(char *line, void *ctx)
{
	struct fetch_ctx *f = ctx;
	char *v;

	if ((v = kv(line, "progress"))) {
		int pct = atoi(v);
		if (pct < 0)   pct = 0;
		if (pct > 100) pct = 100;
		if (f->progress)
			f->progress(pct, 0, f->ctx);
	} else if (kv(line, "verifying")) {
		if (f->progress)
			f->progress(100, 1, f->ctx);
	} else if ((v = kv(line, "error"))) {
		str_copy(f->error, sizeof(f->error), v);
	} else if (kv(line, "staged")) {
		f->staged = 1;
	}
}

int update_fetch(void (*progress)(int pct, int verifying, void *ctx),
                 void *ctx, char *err, unsigned esz)
{
	struct fetch_ctx f = { progress, ctx, "", 0 };
	char *const argv[] = { (char *)UPDATE_SCRIPT, (char *)"update-online", NULL };
	int rc = proc_run_for(argv, cb_fetch, &f, FETCH_MS);

	/* Require both staged output and a clean exit: syncing may follow the output. */
	if (rc == 0 && f.staged)
		return 0;

	if (f.error[0])
		str_copy(err, esz, f.error);
	else if (rc == PROC_TIMEOUT)
		str_copy(err, esz, "Download timed out. Retry to resume.");
	else
		snprintf(err, esz, "Update failed (%d).", rc);
	return -1;
}

int update_set_channel(const char *settings, const char *channel)
{
	return settings_set(settings, "updates.branch", channel);
}

int update_staged(const char *dir)
{
	/* Only verified .tar files are staged; incomplete downloads use .part. */
	DIR *d = opendir(dir);
	if (!d)
		return 0;
	int found = 0;
	struct dirent *e;
	while (!found && (e = readdir(d))) {
		size_t n = strlen(e->d_name);
		found = n > 4 && strcmp(e->d_name + n - 4, ".tar") == 0;
	}
	closedir(d);
	return found;
}
