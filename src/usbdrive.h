/* Copyright (C) 2026-present PortareOS */
/* Disk images a PC can boot from the device, served by usbgadget usb_drive. */
#ifndef PL_USBDRIVE_H
#define PL_USBDRIVE_H

#include <stddef.h>

#define USBDRIVE_DIR "/storage/usbdrive"
#define USBDRIVE_MAX 32

struct image {
	char file[128];      /* "cachyos-desktop-linux-260809.iso" */
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

/* "3.2 GB" from a gigabyte up, "700 MB" below, decimal like a drive's label. */
void usbdrive_size(long long bytes, char *out, size_t osz);

#endif
