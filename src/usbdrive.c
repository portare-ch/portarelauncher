/* Copyright (C) 2026-present PortareOS */
#include "usbdrive.h"
#include "text.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int is_image(const char *file)
{
	const char *dot = strrchr(file, '.');
	return dot && dot != file &&
	       (strcasecmp(dot, ".iso") == 0 || strcasecmp(dot, ".img") == 0);
}

static int by_name(const void *a, const void *b)
{
	return strcasecmp(((const struct image *)a)->name,
	                  ((const struct image *)b)->name);
}

int usbdrive_load(struct usbdrive *d)
{
	return usbdrive_load_from(d, USBDRIVE_DIR);
}

int usbdrive_load_from(struct usbdrive *d, const char *dir)
{
	memset(d, 0, sizeof(*d));
	DIR *dh = opendir(dir);
	if (!dh)
		return 0;
	str_copy(d->dir, sizeof(d->dir), dir);

	struct dirent *e;
	while ((e = readdir(dh)) && d->n < USBDRIVE_MAX) {
		if (e->d_name[0] == '.' || strlen(e->d_name) >= sizeof(d->e[0].file) ||
		    !is_image(e->d_name))
			continue;

		struct stat st;
		if (fstatat(dirfd(dh), e->d_name, &st, 0) != 0 || !S_ISREG(st.st_mode))
			continue;

		struct image *im = &d->e[d->n++];
		str_copy(im->file, sizeof(im->file), e->d_name);
		str_copy(im->name, sizeof(im->name), e->d_name);
		*strrchr(im->name, '.') = '\0';
		im->bytes = (long long)st.st_size;
	}
	closedir(dh);

	qsort(d->e, (size_t)d->n, sizeof(d->e[0]), by_name);
	return d->n;
}

int usbdrive_find(const struct usbdrive *d, const char *path)
{
	size_t len = strlen(d->dir);
	if (!d->dir[0] || strncmp(path, d->dir, len) != 0 || path[len] != '/')
		return -1;
	for (int i = 0; i < d->n; i++)
		if (strcmp(path + len + 1, d->e[i].file) == 0)
			return i;
	return -1;
}

void usbdrive_size(long long bytes, char *out, size_t osz)
{
	if (bytes >= 1000000000LL)
		snprintf(out, osz, "%.1f GB", (double)bytes / 1e9);
	else
		snprintf(out, osz, "%lld MB", (bytes + 500000) / 1000000);
}
