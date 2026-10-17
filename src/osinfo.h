/* Installed build metadata from /etc/os-release. */
#ifndef PL_OSINFO_H
#define PL_OSINFO_H

#define OS_RELEASE "/etc/os-release"

struct osinfo {
	char version[16];   /* OS_VERSION   "20260923" */
	char build[16];     /* OS_BUILD     "nightly" */
	char commit[48];    /* BUILD_ID     the full commit */
	char branch[32];    /* BUILD_BRANCH "next" */
	char date[40];      /* BUILD_DATE   "Wed Sep 23 00:53:32 UTC 2026" */
	char device[24];    /* HW_DEVICE    "SM8550" */
	char cpu[40];       /* HW_CPU       "Snapdragon 8 Gen2" */
};

/* Missing fields stay empty. Returns 0, or -1 if the file cannot be read. */
int osinfo_read(const char *path, struct osinfo *o);

#endif
