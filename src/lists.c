#include "lists.h"

#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "text.h"

/* mkdir -p for the file's directory, so the first save does not need a
 * package to have made it. */
static void make_parents(const char *file)
{
	char tmp[512];
	str_copy(tmp, sizeof(tmp), file);
	char *dir = dirname(tmp);
	char path[512] = "";
	for (char *p = dir; *p; p++) {
		if (*p == '/' && p != dir) {
			*p = '\0';
			mkdir(dir, 0755);
			*p = '/';
		}
	}
	str_copy(path, sizeof(path), dir);
	mkdir(path, 0755);
}

/* Written to a sibling and renamed over the old file, so a power cut mid
 * write leaves the previous list rather than half of one. */
static FILE *open_replacement(const char *file, char *tmp, size_t tsz)
{
	make_parents(file);
	snprintf(tmp, tsz, "%s.new", file);
	return fopen(tmp, "w");
}

static int commit_replacement(FILE *fp, const char *tmp, const char *file)
{
	if (fclose(fp) != 0)
		return -1;
	return rename(tmp, file) == 0 ? 0 : -1;
}

static void chomp(char *s)
{
	size_t n = strlen(s);
	while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r'))
		s[--n] = '\0';
}

/* ---- recent ----------------------------------------------------------- */

int recents_load(struct recents *r, const char *file)
{
	memset(r, 0, sizeof(*r));
	FILE *fp = fopen(file, "r");
	if (!fp)
		return errno == ENOENT ? 0 : -1;

	char line[640];
	while (r->n < RECENT_MAX && fgets(line, sizeof(line), fp)) {
		chomp(line);
		char *a = strchr(line, '\t');
		if (!a)
			continue;
		*a++ = '\0';
		char *b = strchr(a, '\t');
		if (!b)
			continue;
		*b++ = '\0';
		if (!*b)
			continue;
		struct recent *e = &r->e[r->n++];
		e->when = strtoll(line, NULL, 10);
		str_copy(e->sys, sizeof(e->sys), a);
		str_copy(e->path, sizeof(e->path), b);
	}
	fclose(fp);
	return 0;
}

int recents_save(const struct recents *r, const char *file)
{
	char tmp[520];
	FILE *fp = open_replacement(file, tmp, sizeof(tmp));
	if (!fp)
		return -1;
	for (int i = 0; i < r->n; i++)
		fprintf(fp, "%lld\t%s\t%s\n", r->e[i].when, r->e[i].sys, r->e[i].path);
	return commit_replacement(fp, tmp, file);
}

void recents_add(struct recents *r, const char *sys, const char *path,
                 long long when)
{
	/* Out with the older entry for the same game first, so the list is
	 * ten games and not ten launches. */
	for (int i = 0; i < r->n; i++) {
		if (strcmp(r->e[i].path, path) == 0) {
			memmove(&r->e[i], &r->e[i + 1],
			        (size_t)(r->n - i - 1) * sizeof(r->e[0]));
			r->n--;
			break;
		}
	}
	if (r->n == RECENT_MAX)
		r->n--;
	memmove(&r->e[1], &r->e[0], (size_t)r->n * sizeof(r->e[0]));
	r->n++;
	struct recent *e = &r->e[0];
	memset(e, 0, sizeof(*e));
	str_copy(e->sys, sizeof(e->sys), sys);
	str_copy(e->path, sizeof(e->path), path);
	e->when = when;
}

int recents_prune(struct recents *r, int (*exists)(const char *path))
{
	int kept = 0;
	for (int i = 0; i < r->n; i++)
		if (exists(r->e[i].path))
			r->e[kept++] = r->e[i];
	int gone = r->n - kept;
	r->n = kept;
	return gone;
}

/* ---- favourites ------------------------------------------------------- */

void favs_free(struct favs *f)
{
	free(f->path);
	memset(f, 0, sizeof(*f));
}

static int favs_grow(struct favs *f)
{
	if (f->n < f->cap)
		return 0;
	int cap = f->cap ? f->cap * 2 : 16;
	char (*p)[512] = realloc(f->path, (size_t)cap * sizeof(*p));
	if (!p)
		return -1;
	f->path = p;
	f->cap = cap;
	return 0;
}

int favs_load(struct favs *f, const char *file)
{
	favs_free(f);
	FILE *fp = fopen(file, "r");
	if (!fp)
		return errno == ENOENT ? 0 : -1;

	char line[640];
	while (fgets(line, sizeof(line), fp)) {
		chomp(line);
		if (!line[0] || favs_has(f, line) || favs_grow(f) < 0)
			continue;
		str_copy(f->path[f->n++], sizeof(f->path[0]), line);
	}
	fclose(fp);
	return 0;
}

int favs_save(const struct favs *f, const char *file)
{
	char tmp[520];
	FILE *fp = open_replacement(file, tmp, sizeof(tmp));
	if (!fp)
		return -1;
	for (int i = 0; i < f->n; i++)
		fprintf(fp, "%s\n", f->path[i]);
	return commit_replacement(fp, tmp, file);
}

int favs_has(const struct favs *f, const char *path)
{
	for (int i = 0; i < f->n; i++)
		if (strcmp(f->path[i], path) == 0)
			return 1;
	return 0;
}

int favs_toggle(struct favs *f, const char *path)
{
	for (int i = 0; i < f->n; i++) {
		if (strcmp(f->path[i], path) == 0) {
			memmove(f->path[i], f->path[i + 1],
			        (size_t)(f->n - i - 1) * sizeof(f->path[0]));
			f->n--;
			return 0;
		}
	}
	if (favs_grow(f) < 0)
		return 0;
	str_copy(f->path[f->n++], sizeof(f->path[0]), path);
	return 1;
}

int favs_prune(struct favs *f, int (*exists)(const char *path))
{
	int kept = 0;
	for (int i = 0; i < f->n; i++)
		if (exists(f->path[i]))
			memmove(f->path[kept++], f->path[i], sizeof(f->path[0]));
	int gone = f->n - kept;
	f->n = kept;
	return gone;
}

/* ---- headings and names ------------------------------------------------ */

/* Midnight of the local day `t` falls on, so two launches are on the same
 * day when their midnights agree, whatever the hour. */
static long long midnight(long long t)
{
	time_t tt = (time_t)t;
	struct tm tm;
	localtime_r(&tt, &tm);
	tm.tm_hour = tm.tm_min = tm.tm_sec = 0;
	tm.tm_isdst = -1;
	return (long long)mktime(&tm);
}

void day_label(long long when, long long now, char *out, size_t osz)
{
	static const char *const days[7] = {
		"SUNDAY", "MONDAY", "TUESDAY", "WEDNESDAY", "THURSDAY",
		"FRIDAY", "SATURDAY" };
	static const char *const months[12] = {
		"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
		"JUL", "AUG", "SEP", "OCT", "NOV", "DEC" };

	/* Rounded, not truncated: a DST change makes a day 23 or 25 hours. */
	long long diff = midnight(now) - midnight(when);
	int ago = (int)((diff + 43200) / 86400);

	time_t tt = (time_t)when;
	struct tm tm;
	localtime_r(&tt, &tm);

	if (ago <= 0)
		snprintf(out, osz, "TODAY");
	else if (ago == 1)
		snprintf(out, osz, "YESTERDAY");
	else if (ago < 7)
		snprintf(out, osz, "%s", days[tm.tm_wday]);
	else
		snprintf(out, osz, "%d %s", tm.tm_mday, months[tm.tm_mon]);
}

void short_system(const char *name, char *out, size_t osz)
{
	static const struct { const char *name, *label; } table[] = {
		{ "snes", "SNES" }, { "snesh", "SNES" }, { "snesmsu1", "SNES" },
		{ "sfc", "SFC" }, { "satellaview", "SNES" },
		{ "nes", "NES" }, { "famicom", "FC" }, { "fds", "FDS" },
		{ "gb", "GB" }, { "gbh", "GB" }, { "gbc", "GBC" }, { "gbch", "GBC" },
		{ "gba", "GBA" }, { "gbah", "GBA" }, { "gbav", "GBA" },
		{ "n64", "N64" }, { "gamecube", "GC" }, { "wii", "WII" },
		{ "nds", "NDS" },
		{ "psx", "PS1" }, { "ps2", "PS2" }, { "psp", "PSP" },
		{ "genesis", "MD" }, { "megadrive", "MD" }, { "segacd", "MCD" },
		{ "sega32x", "32X" }, { "mastersystem", "SMS" }, { "gamegear", "GG" },
		{ "saturn", "SAT" }, { "dreamcast", "DC" },
		{ "arcade", "ARC" }, { "fbneo", "ARC" }, { "neogeo", "NEO" },
		{ "neocd", "NCD" }, { "scummvm", "SCUMM" }, { "xbox", "XBOX" },
	};
	for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
		if (strcmp(table[i].name, name) == 0) {
			snprintf(out, osz, "%s", table[i].label);
			return;
		}
	}
	size_t n = 0;
	for (; name[n] && n < 4 && n + 1 < osz; n++)
		out[n] = (char)toupper((unsigned char)name[n]);
	out[n] = '\0';
}
