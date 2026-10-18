#ifndef PL_DISC_TITLES_H
#define PL_DISC_TITLES_H
#include <stddef.h>
#include <stdint.h>
#include "proc.h"
#include "ps_serial.h"
struct game;
struct psystem;
struct catalog;
struct disc_title;
struct disc_identity;
struct disc_signature { uint64_t v[7]; };
struct disc_title_table {
	char *table;
	struct disc_title *titles;
	size_t ntitles;
};
struct disc_titles {
	struct disc_title_table ps2, psx, psp, neocd;
	struct disc_identity *entries;
	int *buckets;
	size_t nentries, capacity;
	char cache[512], helper[512];
	int dirty;
	struct proc_job job;
	struct psystem *queue;
	char (*paths)[512];
	int npaths;
	int cursor;
	char pending[512], completed[512];
	struct disc_signature signature;
};
/* Missing/damaged files are an empty database/cache. init always initializes. */
void disc_titles_init(struct disc_titles *m, const char *ps2_table, const char *psx_table,
                      const char *psp_table, const char *neocd_table, const char *cache, const char *helper);
void disc_titles_free(struct disc_titles *m);
int disc_titles_supported(const char *system);
int disc_titles_eligible(const char *system, const char *path, int folder);
const char *disc_titles_lookup(const struct disc_titles *m, const char *system, const char *serial);
/* Changes only the display name, and only for a current cached identity. */
void disc_titles_apply(const struct disc_titles *m, const char *system, struct game *g);
/* begin queues work; step starts/collects at most one helper outside rendering/input. */
void disc_titles_begin(struct disc_titles *m, struct psystem *s);
void disc_titles_begin_catalog(struct disc_titles *m, struct catalog *catalog);
int disc_titles_step(struct disc_titles *m);
void disc_titles_stop(struct disc_titles *m);
int disc_titles_save(struct disc_titles *m);
/* Copy complete UTF-8 characters within the destination capacity. */
void disc_title_copy(char *dst, size_t size, const char *src);
#endif
