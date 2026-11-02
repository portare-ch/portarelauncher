/* Copyright (C) 2026-present PortareOS */
/* Disk images served through usbgadget usb_drive. */
#ifndef PL_USBDRIVE_H
#define PL_USBDRIVE_H

#include <stddef.h>

#define USBDRIVE_DIR "/storage/usbdrive"
#define USBDRIVE_MAX 32

struct image {
	char file[128];
	char name[128];      /* the file name without its extension */
	long long bytes;
};

struct usbdrive {
	char dir[64];
	struct image e[USBDRIVE_MAX];
	int n;
};

/* Lists .iso and .img files by name. Returns the count, 0 for a missing folder. */
int usbdrive_load(struct usbdrive *d);
int usbdrive_load_from(struct usbdrive *d, const char *dir);

/* Index of the image at path, or -1. */
int usbdrive_find(const struct usbdrive *d, const char *path);

/* Decimal MB below 1 GB, otherwise GB with one decimal place. */
void usbdrive_size(long long bytes, char *out, size_t osz);

#endif
