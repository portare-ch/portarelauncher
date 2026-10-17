/* Cross-system lists, atomically rewritten on change:
 *   recent      "<epoch>\t<system>\t<path>", newest first, at most ten
 *   favourites  "<path>", in insertion order
 * Paths match the catalog; missing games are pruned when a list opens. */
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

/* A missing file is an empty list; -1 if an existing file cannot be read. */
int  recents_load(struct recents *r, const char *file);
int  recents_save(const struct recents *r, const char *file);
/* Prepends the game, removing duplicates and entries beyond RECENT_MAX. */
void recents_add(struct recents *r, const char *sys, const char *path,
                 long long when);
/* Drops the entries exists() rejects; returns how many went. */
int  recents_prune(struct recents *r, int (*exists)(const char *path));

void favs_free(struct favs *f);
int  favs_load(struct favs *f, const char *file);
int  favs_save(const struct favs *f, const char *file);
int  favs_has(const struct favs *f, const char *path);
/* Returns 1 if the path is now a favourite, 0 if removed. */
int  favs_toggle(struct favs *f, const char *path);
int  favs_prune(struct favs *f, int (*exists)(const char *path));

/* Local-date heading: today, yesterday, weekday within the last week,
 * otherwise date. ja selects Japanese labels. */
void day_label(long long when, long long now, int ja, char *out, size_t osz);

/* Short system label: SNES, PS1, etc.; SFC, FC, PS in Japanese.
 * Unknown names are uppercased and truncated to four characters. */
void short_system(const char *name, int ja, char *out, size_t osz);

#endif
