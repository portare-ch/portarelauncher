/* System updates, through portareos-update.
 *
 * The script does the work - asking GitHub what the channel offers,
 * downloading, checking the .sha256, staging the .tar where the next boot
 * applies it - and prints key=value lines. This file runs it and reads them,
 * the same split as nmcli and net.c: no HTTP, JSON or checksums in here.
 *
 * Two channels, kept in updates.branch in system.cfg: "nightly" takes the
 * newest build of either kind, "release" only the monthly releases. Anything
 * else ("auto", the default) follows the build that is installed.
 */
#ifndef PL_UPDATE_H
#define PL_UPDATE_H

enum update_state {
	UPD_UNKNOWN = 0,   /* not asked yet                                  */
	UPD_AVAILABLE,     /* newer than what is installed                   */
	UPD_CURRENT,       /* it is what is installed                        */
	UPD_NEWER,         /* the installed build is newer than the channel  */
	UPD_NONE,          /* nothing published on the channel for us yet    */
	UPD_ERROR,         /* could not ask; error says why                  */
};

struct update_info {
	enum update_state state;
	char channel[16];      /* "nightly" or "release", as resolved     */
	char installed[40];    /* "20260923 2a9c631"                      */
	char tag[64];          /* "nightly-20260924-115"                  */
	long long size;        /* bytes of the .tar, 0 when unknown       */
	char error[96];
};

/* Asks GitHub, through the script. Blocks for a second or two - up to the
 * script's own 30-second limit when the network is bad. */
void update_check(struct update_info *u);

/* Downloads, verifies and stages the channel's newest build. progress is
 * called with 0-100 as the download moves, then once with verifying set
 * while the checksum is computed. Blocks for as long as the download takes.
 * Returns 0 when the update is staged for the next boot; otherwise -1 with
 * the reason in err. */
int update_fetch(void (*progress)(int pct, int verifying, void *ctx),
                 void *ctx, char *err, unsigned esz);

/* Is a downloaded update waiting in dir for the next boot? */
int update_staged(const char *dir);

/* Writes updates.branch. */
int update_set_channel(const char *settings, const char *channel);

#endif
