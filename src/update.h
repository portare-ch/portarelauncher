/* Updates via portareos-update, which checks GitHub, downloads, verifies
 * and stages builds, reporting key=value lines. updates.branch selects
 * "nightly" (newest build), "release" (monthly), or "auto" (installed channel). */
#ifndef PL_UPDATE_H
#define PL_UPDATE_H

enum update_state {
	UPD_UNKNOWN = 0,   /* not asked yet */
	UPD_AVAILABLE,     /* newer than what is installed */
	UPD_CURRENT,       /* it is what is installed */
	UPD_NEWER,         /* the installed build is newer than the channel */
	UPD_NONE,          /* nothing published on the channel for us yet */
	UPD_ERROR,         /* could not ask; error says why */
};

struct update_info {
	enum update_state state;
	char channel[16];      /* "nightly" or "release", as resolved */
	char installed[40];    /* "20260923 2a9c631" */
	char tag[64];          /* "nightly-20260924-115" */
	long long size;        /* bytes of the .tar, 0 when unknown */
	char error[96];
};

/* Runs the check script with a 60-second deadline. */
void update_check(struct update_info *u);

/* Downloads, verifies and stages a build. progress receives 0-100, then
 * a call with verifying set during checksum verification. Blocks until done.
 * Returns 0 when staged for next boot, or -1 with the reason in err. */
int update_fetch(void (*progress)(int pct, int verifying, void *ctx),
                 void *ctx, char *err, unsigned esz);

/* Is a downloaded update waiting in dir for the next boot? */
int update_staged(const char *dir);

/* Writes updates.branch. */
int update_set_channel(const char *settings, const char *channel);

#endif
