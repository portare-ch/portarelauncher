/* The two cross-system lists: what was played last, and what the player
 * marked. Both are files under the launcher's config directory, one entry a
 * line, written whole on every change; they are ten and a few dozen lines,
 * and a rewrite is simpler to reason about than an edit in place.
 *
 *   recent      "<epoch>\t<system>\t<path>", newest first, at most ten
 *   favourites  "<path>", in the order they were added
 *
 * Paths are the catalog's game paths, so a renamed or deleted ROM is found
 * missing on the next read and dropped rather than shown as a dead row. */
#ifndef PL_LISTS_H
#define PL_LISTS_H
#include <stddef.h>

#define RECENT_MAX 10

struct recent {
	char sys[32];
	char path[512];
	long long when;    /* seconds since the epoch, when the game exited */
};

struct recents {
	struct recent e[RECENT_MAX];
	int n;
};

struct favs {
	char (*path)[512];
	int n, cap;
};

/* A missing file is an empty list and not an error; -1 only when the file
 * exists and cannot be read. */
int  recents_load(struct recents *r, const char *file);
int  recents_save(const struct recents *r, const char *file);
/* To the front, dropping an older entry for the same path and whatever
 * falls off the end. */
void recents_add(struct recents *r, const char *sys, const char *path,
                 long long when);
/* Drops the entries exists() rejects; returns how many went. */
int  recents_prune(struct recents *r, int (*exists)(const char *path));

void favs_free(struct favs *f);
int  favs_load(struct favs *f, const char *file);
int  favs_save(const struct favs *f, const char *file);
int  favs_has(const struct favs *f, const char *path);
/* Returns 1 when the path is a favourite afterwards, 0 when it was one
 * and is not now. */
int  favs_toggle(struct favs *f, const char *path);
int  favs_prune(struct favs *f, int (*exists)(const char *path));

/* The heading a launch at `when` sits under, as the clock reads `now`:
 * TODAY, YESTERDAY, the weekday for the six days before that, and the
 * date, "29 SEP", for anything older. Local time, like the clock. */
void day_label(long long when, long long now, char *out, size_t osz);

/* What a system is called in a list that mixes them: "snes" is SNES,
 * "psx" is PS1. A name not in the table is upper-cased and cut to four. */
void short_system(const char *name, char *out, size_t osz);

#endif
