#include "catalog.h"
#include "settings.h"
#include "sheets.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* Truncating is deliberate everywhere this is used: a name longer than its
 * field is clipped rather than rejected. */
static void copy_str(char *dst, size_t dsz, const char *src)
{
	size_t n = strlen(src);
	if (n >= dsz)
		n = dsz - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

static void copy_trim(char *dst, size_t dsz, const char *src, size_t n)
{
	while (n && isspace((unsigned char)*src)) { src++; n--; }
	while (n && isspace((unsigned char)src[n - 1])) n--;
	if (n >= dsz)
		n = dsz - 1;
	memcpy(dst, src, n);
	dst[n] = '\0';
}

/* The five entities an XML generator will emit. Decoded in place. */
static void unescape(char *s)
{
	static const struct { const char *ent; char ch; } map[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
		{ "&apos;", '\'' }, { "&quot;", '"' },
	};
	char *r = s, *w = s;
	while (*r) {
		if (*r == '&') {
			size_t i;
			for (i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
				size_t l = strlen(map[i].ent);
				if (strncmp(r, map[i].ent, l) == 0) {
					*w++ = map[i].ch;
					r += l;
					break;
				}
			}
			if (i < sizeof(map) / sizeof(map[0]))
				continue;
		}
		*w++ = *r++;
	}
	*w = '\0';
}

/* Pulls <tag>value</tag> out of one line, and <tag attr="x">value</tag> too.
 *
 * The attribute case is not decoration: the default core is written
 * <core default="true">swanstation</core>, and a matcher that only accepts
 * "<core>" walks straight past it and launches with no core at all. */
static int tag_value(const char *line, const char *tag, char *out, size_t osz)
{
	char open[64], close[64];
	snprintf(open, sizeof(open), "<%s", tag);
	snprintf(close, sizeof(close), "</%s>", tag);

	const char *a = strstr(line, open);
	if (!a)
		return 0;
	a += strlen(open);
	/* The name has to end here, so <path> does not match <platform>. */
	if (*a != '>' && *a != ' ' && *a != '\t')
		return 0;
	a = strchr(a, '>');
	if (!a)
		return 0;
	a += 1;
	const char *b = strstr(a, close);
	if (!b)
		return 0;
	copy_trim(out, osz, a, (size_t)(b - a));
	unescape(out);
	return 1;
}

static int ext_matches(const char *file, const char *exts)
{
	const char *dot = strrchr(file, '.');
	if (!dot)
		return 0;

	/* exts is a space-separated list of ".cue .chd". Compare case
	 * insensitively, since roms arrive named however they arrive. */
	const char *p = exts;
	while (*p) {
		while (*p == ' ') p++;
		const char *q = p;
		while (*q && *q != ' ') q++;
		size_t n = (size_t)(q - p);
		if (n && strlen(dot) == n && strncasecmp(dot, p, n) == 0)
			return 1;
		p = q;
	}
	return 0;
}

static void add_game(struct psystem *s, const char *path, const char *file)
{
	struct game *g = realloc(s->games, (size_t)(s->ngames + 1) * sizeof(*g));
	if (!g)
		return;
	s->games = g;
	g = &s->games[s->ngames];

	snprintf(g->path, sizeof(g->path), "%s/%s", path, file);
	copy_str(g->name, sizeof(g->name), file);
	char *dot = strrchr(g->name, '.');
	if (dot)
		*dot = '\0';
	s->ngames++;
}

static void scan(struct psystem *s, const char *dir, int depth)
{
	DIR *d = opendir(dir);
	if (!d)
		return;

	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.')
			continue;
		/* Artwork and metadata live alongside the roms. */
		if (!strcmp(e->d_name, "images") || !strcmp(e->d_name, "media") ||
		    !strcmp(e->d_name, "gamelist.xml"))
			continue;

		char full[768];
		snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);

		struct stat st;
		if (stat(full, &st) < 0)
			continue;

		if (S_ISDIR(st.st_mode)) {
			/* PS2 and Dreamcast titles arrive as a directory holding
			 * the disc image, so one level down is worth looking at. */
			if (depth > 0)
				scan(s, full, depth - 1);
		} else if (ext_matches(e->d_name, s->exts)) {
			add_game(s, dir, e->d_name);
		}
	}
	closedir(d);
}

static int by_path(const void *a, const void *b)
{
	return strcasecmp((const char *)a, (const char *)b);
}

/* Drops every game that a sheet in the same system names, so a game made
 * of a .cue and its tracks, or an .m3u and its discs, is listed once, as
 * its sheet. Names are compared without regard to case: a sheet written on
 * Windows often spells its tracks differently from the files on the card. */
static void hide_referenced(struct psystem *s)
{
	char (*refs)[512] = NULL;
	int nrefs = 0, cap = 0;

	for (int i = 0; i < s->ngames; i++) {
		if (!sheet_is(s->games[i].path))
			continue;
		if (nrefs + SHEET_MAX_REFS > cap) {
			int ncap = cap ? cap * 2 : 256;
			while (ncap < nrefs + SHEET_MAX_REFS)
				ncap *= 2;
			char (*r)[512] = realloc(refs, (size_t)ncap * sizeof(*refs));
			if (!r)
				break;
			refs = r;
			cap = ncap;
		}
		nrefs += sheet_refs(s->games[i].path, refs + nrefs, SHEET_MAX_REFS);
	}
	if (nrefs == 0) {
		free(refs);
		return;
	}

	qsort(refs, (size_t)nrefs, sizeof(*refs), by_path);
	int w = 0;
	for (int i = 0; i < s->ngames; i++) {
		if (bsearch(s->games[i].path, refs, (size_t)nrefs, sizeof(*refs), by_path))
			continue;
		s->games[w++] = s->games[i];
	}
	s->ngames = w;
	free(refs);
}

static int by_name(const void *a, const void *b)
{
	return strcasecmp(((const struct game *)a)->name,
	                  ((const struct game *)b)->name);
}

static int by_fullname(const void *a, const void *b)
{
	return strcasecmp(((const struct psystem *)a)->fullname,
	                  ((const struct psystem *)b)->fullname);
}

int catalog_load(struct catalog *c, const char *es_systems, const char *settings)
{
	memset(c, 0, sizeof(*c));

	FILE *f = fopen(es_systems, "r");
	if (!f) {
		fprintf(stderr, "open %s failed\n", es_systems);
		return -1;
	}

	struct psystem cur;
	int in_system = 0;
	char line[2048];

	while (fgets(line, sizeof(line), f)) {
		if (strstr(line, "<system>")) {
			memset(&cur, 0, sizeof(cur));
			in_system = 1;
			continue;
		}
		if (!in_system)
			continue;

		if (strstr(line, "</system>")) {
			in_system = 0;
			if (!cur.name[0] || !cur.path[0])
				continue;
			/* EmulationStation's Tools system: no core, so it could only
			 * ever say "Cannot launch". The launcher's own Tools row runs
			 * the same folder (tools.c), and listing both put two rows
			 * called Tools in the list. */
			if (strcmp(cur.name, "tools") == 0)
				continue;

			{
				char key[96], v[64];
				snprintf(key, sizeof(key), "%s.core", cur.name);
				if (settings_get(settings, key, v, sizeof(v)))
					copy_str(cur.core, sizeof(cur.core), v);
				snprintf(key, sizeof(key), "%s.emulator", cur.name);
				if (settings_get(settings, key, v, sizeof(v)))
					copy_str(cur.emulator, sizeof(cur.emulator), v);
			}

			scan(&cur, cur.path, 1);
			hide_referenced(&cur);
			if (cur.ngames == 0) {
				free(cur.games);
				continue;
			}
			qsort(cur.games, (size_t)cur.ngames, sizeof(*cur.games), by_name);

			struct psystem *ns = realloc(c->sys, (size_t)(c->n + 1) * sizeof(*ns));
			if (!ns) {
				free(cur.games);
				continue;
			}
			c->sys = ns;
			c->sys[c->n++] = cur;
			continue;
		}

		char v[512];
		if (tag_value(line, "name", v, sizeof(v)) && !cur.name[0])
			copy_str(cur.name, sizeof(cur.name), v);
		else if (tag_value(line, "fullname", v, sizeof(v)))
			copy_str(cur.fullname, sizeof(cur.fullname), v);
		else if (tag_value(line, "path", v, sizeof(v)))
			copy_str(cur.path, sizeof(cur.path), v);
		else if (tag_value(line, "extension", v, sizeof(v)))
			copy_str(cur.exts, sizeof(cur.exts), v);
		else if (tag_value(line, "command", v, sizeof(v))) {
			/* Only the binary is taken. The arguments are built as an
			 * argv rather than by substituting into a shell string, so
			 * a game called Ratchet & Clank cannot become two words. */
			char *sp = strchr(v, ' ');
			if (sp)
				*sp = '\0';
			copy_str(cur.launcher, sizeof(cur.launcher), v);
		} else if (tag_value(line, "core", v, sizeof(v))) {
			if (!cur.core[0] && strstr(line, "default=\"true\""))
				copy_str(cur.core, sizeof(cur.core), v);
		} else {
			const char *a = strstr(line, "<emulator name=\"");
			if (a && !cur.emulator[0]) {
				a += strlen("<emulator name=\"");
				const char *b = strchr(a, '"');
				if (b)
					copy_trim(cur.emulator, sizeof(cur.emulator), a,
					          (size_t)(b - a));
			}
		}
	}

	fclose(f);

	if (c->n == 0) {
		fprintf(stderr, "no systems with games found\n");
		return -1;
	}
	qsort(c->sys, (size_t)c->n, sizeof(*c->sys), by_fullname);
	return 0;
}

void catalog_free(struct catalog *c)
{
	for (int i = 0; i < c->n; i++)
		free(c->sys[i].games);
	free(c->sys);
	c->sys = NULL;
	c->n = 0;
}
