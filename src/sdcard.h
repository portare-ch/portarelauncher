/* Copyright (C) 2026-present PortareOS */
/* SD card status and formatting through PortareOS's sdcard helper. */
#ifndef PL_SDCARD_H
#define PL_SDCARD_H

#include <stddef.h>

#define SDCARD "/usr/bin/sdcard"
/* Stop the formatter's process group before returning on timeout. */
#define SDCARD_FORMAT_MS (10 * 60 * 1000)
#define SDCARD_RESTORE_MS (60 * 1000)

enum sd_state {
	SD_UNKNOWN,   /* helper unavailable or no valid status */
	SD_NONE,      /* no card inserted */
	SD_READY,     /* F2FS: games storage */
	SD_FORMAT,    /* any other filesystem, or none */
	SD_SYSTEM,    /* the running system is on the card */
	SD_SMALL,     /* below the OS's minimum size */
};

enum sd_error { SDE_OK, SDE_NONE, SDE_SYSTEM, SDE_SMALL, SDE_BUSY, SDE_CHANGED,
                SDE_FAILED };

struct sdcard {
	enum sd_state state;
	long long bytes;
	char name[32];        /* product name, such as SN128 */
	char id[40];          /* CID used to bind erase confirmation */
};

void sdcard_read(struct sdcard *c);

/* Parse one state=, bytes=, name= or id= status line. */
void sdcard_line(struct sdcard *c, const char *line);

/* Requires an ID and an eligible card state. */
int sdcard_can_format(const struct sdcard *c);

/* Confirm that the same eligible card is still inserted. */
int sdcard_confirm(const struct sdcard *now, const char *armed_id);

/* Blocks while formatting the matching card as F2FS. Stops remaining workers
 * before returning; attempts to restore mounts after an unexplained failure. */
enum sd_error sdcard_format(const char *id);

/* Decimal GB/TB; one decimal below 10 GB or for fractional TB. */
void sdcard_size(long long bytes, char *out, size_t osz);

#endif
