/* Copyright (C) 2026-present PortareOS */
#include "sdcard.h"
#include "proc.h"
#include "text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const state_words[] = {
	[SD_NONE] = "none", [SD_READY] = "ready", [SD_FORMAT] = "format",
	[SD_SYSTEM] = "system", [SD_SMALL] = "small",
};

static const char *const error_words[] = {
	[SDE_NONE] = "none", [SDE_SYSTEM] = "system", [SDE_SMALL] = "small",
	[SDE_BUSY] = "busy", [SDE_CHANGED] = "changed", [SDE_FAILED] = "failed",
};

void sdcard_line(struct sdcard *c, const char *line)
{
	if (strncmp(line, "state=", 6) == 0) {
		c->state = SD_UNKNOWN;
		for (size_t i = 0; i < sizeof(state_words) / sizeof(state_words[0]); i++)
			if (state_words[i] && strcmp(line + 6, state_words[i]) == 0)
				c->state = (enum sd_state)i;
	} else if (strncmp(line, "bytes=", 6) == 0) {
		c->bytes = strtoll(line + 6, NULL, 10);
	} else if (strncmp(line, "name=", 5) == 0) {
		str_copy(c->name, sizeof(c->name), line + 5);
	} else if (strncmp(line, "id=", 3) == 0) {
		str_copy(c->id, sizeof(c->id), line + 3);
	}
}

static void cb_status(char *line, void *ctx)
{
	sdcard_line(ctx, line);
}

void sdcard_read(struct sdcard *c)
{
	char *const argv[] = { (char *)SDCARD, (char *)"status", NULL };
	memset(c, 0, sizeof(*c));
	if (proc_run(argv, cb_status, c) != 0)
		memset(c, 0, sizeof(*c));
}

int sdcard_can_format(const struct sdcard *c)
{
	return (c->state == SD_READY || c->state == SD_FORMAT) && c->id[0];
}

int sdcard_confirm(const struct sdcard *now, const char *armed_id)
{
	return sdcard_can_format(now) && armed_id[0] && strcmp(now->id, armed_id) == 0;
}

static void cb_error(char *line, void *ctx)
{
	enum sd_error *e = ctx;
	if (strncmp(line, "error=", 6) != 0)
		return;
	*e = SDE_FAILED;
	for (size_t i = 0; i < sizeof(error_words) / sizeof(error_words[0]); i++)
		if (error_words[i] && strcmp(line + 6, error_words[i]) == 0)
			*e = (enum sd_error)i;
}

enum sd_error sdcard_format(const char *id)
{
	char *const argv[] = { (char *)SDCARD, (char *)"format", (char *)id, NULL };
	enum sd_error e = SDE_OK;
	int rc = proc_run_group_for(argv, cb_error, &e, SDCARD_FORMAT_MS);
	if (rc != 0 && e == SDE_OK) {
		/* Workers have stopped; try restoring mounts after an unexplained exit. */
		char *const restore[] = { (char *)SDCARD, (char *)"restore", NULL };
		proc_run_for(restore, NULL, NULL, SDCARD_RESTORE_MS);
		return SDE_FAILED;
	}
	return e;
}

void sdcard_size(long long bytes, char *out, size_t osz)
{
	if (bytes >= 1000000000000LL) {
		long long tenths = (bytes + 50000000000LL) / 100000000000LL;
		if (tenths % 10 == 0)
			snprintf(out, osz, "%lld TB", tenths / 10);
		else
			snprintf(out, osz, "%lld.%lld TB", tenths / 10, tenths % 10);
	} else if (bytes >= 10000000000LL)
		snprintf(out, osz, "%lld GB", (bytes + 500000000) / 1000000000);
	else
		snprintf(out, osz, "%.1f GB", (double)bytes / 1e9);
}
