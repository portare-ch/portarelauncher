#include "tools.h"
#include "proc.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static int is_dir(const char *path)
{
	struct stat st;
	return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static char *read_all(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
		return NULL;
	/* The one this reads is 15 KB. The cap only stops a stray file of
	 * the same name from being read whole into memory. */
	size_t cap = 256 * 1024, len = 0;
	char *buf = malloc(cap + 1);
	if (buf) {
		len = fread(buf, 1, cap, f);
		buf[len] = '\0';
	}
	fclose(f);
	return buf;
}

/* The five entities XML defines; gamelist.xml uses &amp; in a few names. */
static void unescape(char *s)
{
	static const struct { const char *ent; char ch; } map[] = {
		{ "&amp;", '&' }, { "&lt;", '<' }, { "&gt;", '>' },
		{ "&quot;", '"' }, { "&apos;", '\'' },
	};
	char *w = s;
	for (const char *r = s; *r; ) {
		int done = 0;
		if (*r == '&') {
			for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
				size_t n = strlen(map[i].ent);
				if (strncmp(r, map[i].ent, n) == 0) {
					*w++ = map[i].ch;
					r += n;
					done = 1;
					break;
				}
			}
		}
		if (!done)
			*w++ = *r++;
	}
	*w = '\0';
}

/* Copies the text of <tag>...</tag> found between from and end. */
static int tag_in(const char *from, const char *end, const char *tag,
                  char *out, size_t osz)
{
	char open[32], close[32];
	snprintf(open, sizeof(open), "<%s>", tag);
	snprintf(close, sizeof(close), "</%s>", tag);

	const char *a = strstr(from, open);
	if (!a || a >= end)
		return 0;
	a += strlen(open);
	const char *b = strstr(a, close);
	if (!b || b > end)
		return 0;

	size_t n = (size_t)(b - a);
	if (n >= osz)
		n = osz - 1;
	memcpy(out, a, n);
	out[n] = '\0';
	unescape(out);
	return 1;
}

/* Fills in name and description from the <game> whose <path> is this
 * file, if the gamelist has one. */
static void describe(struct tool *t, const char *xml)
{
	if (!xml)
		return;

	char want[160];
	snprintf(want, sizeof(want), "<path>./%s</path>", t->file);
	const char *p = strstr(xml, want);
	if (!p)
		return;

	/* The block around it: back to its <game>, on to its </game>. */
	const char *start = xml, *g;
	for (const char *s = xml; (g = strstr(s, "<game>")) && g < p; s = g + 1)
		start = g;
	const char *end = strstr(p, "</game>");
	if (!end)
		end = p + strlen(p);

	char name[sizeof(t->name)];
	if (tag_in(start, end, "name", name, sizeof(name)) && name[0])
		str_copy(t->name, sizeof(t->name), name);
	tag_in(start, end, "desc", t->desc, sizeof(t->desc));
}

static int by_name(const void *a, const void *b)
{
	return strcasecmp(((const struct tool *)a)->name,
	                  ((const struct tool *)b)->name);
}

int tools_load(struct tools *ts)
{
	memset(ts, 0, sizeof(*ts));

	if (is_dir(TOOLS_DIR))
		str_copy(ts->dir, sizeof(ts->dir), TOOLS_DIR);
	else if (is_dir(TOOLS_DIR_FALLBACK))
		str_copy(ts->dir, sizeof(ts->dir), TOOLS_DIR_FALLBACK);
	else
		return 0;

	/* The folder, a slash, and a name as long as a directory entry can be. */
	char path[sizeof(ts->dir) + 1 + 256];
	snprintf(path, sizeof(path), "%s/gamelist.xml", ts->dir);
	char *xml = read_all(path);

	DIR *d = opendir(ts->dir);
	if (!d) {
		free(xml);
		return 0;
	}

	struct dirent *e;
	while ((e = readdir(d)) && ts->n < TOOLS_MAX) {
		size_t len = strlen(e->d_name);
		if (e->d_name[0] == '.' || len < 4 || len >= sizeof(ts->t[0].file) ||
		    strcmp(e->d_name + len - 3, ".sh") != 0)
			continue;

		snprintf(path, sizeof(path), "%s/%s", ts->dir, e->d_name);
		struct stat st;
		if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
			continue;

		struct tool *t = &ts->t[ts->n++];
		str_copy(t->file, sizeof(t->file), e->d_name);
		str_copy(t->name, sizeof(t->name), e->d_name);
		t->name[len - 3 < sizeof(t->name) ? len - 3 : sizeof(t->name) - 1] = '\0';
		describe(t, xml);
	}
	closedir(d);
	free(xml);

	qsort(ts->t, (size_t)ts->n, sizeof(ts->t[0]), by_name);
	return ts->n;
}
