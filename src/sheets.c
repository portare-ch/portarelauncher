#include "sheets.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *ext_of(const char *file)
{
	const char *dot = strrchr(file, '.');
	const char *slash = strrchr(file, '/');
	return dot && (!slash || dot > slash) ? dot : "";
}

int sheet_is(const char *file)
{
	const char *e = ext_of(file);
	return !strcasecmp(e, ".cue") || !strcasecmp(e, ".gdi") ||
	       !strcasecmp(e, ".toc") || !strcasecmp(e, ".m3u") ||
	       !strcasecmp(e, ".ccd");
}

/* dir/name, with a Windows rip's backslashes made slashes and a leading
 * "./" dropped - ".\\Game.bin" is written by some rippers, and the path it
 * resolves to has to be spelt the way the folder scan spells it. An
 * absolute name is kept as it is. */
static void resolve(const char *dir, const char *name, char *out, size_t osz)
{
	char n[512];
	snprintf(n, sizeof(n), "%s", name);
	for (char *p = n; *p; p++)
		if (*p == '\\')
			*p = '/';
	const char *r = n;
	while (r[0] == '.' && r[1] == '/')
		r += 2;

	/* A path too long for the buffer names nothing, rather than a cut-off
	 * prefix that could hide some other file. */
	int w = r[0] == '/' ? snprintf(out, osz, "%s", r)
	                    : snprintf(out, osz, "%s/%s", dir, r);
	if (w < 0 || (size_t)w >= osz)
		out[0] = '\0';
}

/* The n-th whitespace-separated field of line, where a "quoted field" may
 * contain spaces. Returns 1 and fills out when there is one. */
static int field(const char *line, int n, char *out, size_t osz)
{
	const char *p = line;
	for (int i = 0; ; i++) {
		while (*p && isspace((unsigned char)*p))
			p++;
		if (!*p)
			return 0;
		const char *start, *end;
		if (*p == '"') {
			start = ++p;
			while (*p && *p != '"')
				p++;
			end = p;
			if (*p)
				p++;
		} else {
			start = p;
			while (*p && !isspace((unsigned char)*p))
				p++;
			end = p;
		}
		if (i == n) {
			size_t len = (size_t)(end - start);
			if (len >= osz)
				len = osz - 1;
			memcpy(out, start, len);
			out[len] = '\0';
			return len > 0;
		}
	}
}

int sheet_refs(const char *path, char out[][512], int max)
{
	char dir[512];
	snprintf(dir, sizeof(dir), "%s", path);
	char *slash = strrchr(dir, '/');
	if (slash)
		*slash = '\0';
	else
		strcpy(dir, ".");

	const char *e = ext_of(path);
	int n = 0;

	/* CloneCD: the image and subchannel files are named after the sheet. */
	if (!strcasecmp(e, ".ccd")) {
		static const char *const same[] = { ".img", ".sub", ".IMG", ".SUB" };
		size_t base = strlen(path) - strlen(e);
		for (size_t i = 0; i < sizeof(same) / sizeof(same[0]) && n < max; i++)
			snprintf(out[n++], 512, "%.*s%s", (int)base, path, same[i]);
		return n;
	}

	FILE *f = fopen(path, "r");
	if (!f)
		return 0;

	char line[1024], name[512];
	int lineno = 0;
	/* A sheet is a few lines. A file this large with a sheet's extension
	 * is not one, and is not read to the end. */
	while (n < max && lineno < 4096 && fgets(line, sizeof(line), f)) {
		lineno++;
		line[strcspn(line, "\r\n")] = '\0';

		if (!strcasecmp(e, ".m3u")) {
			/* One entry per line; # starts a comment or a directive. */
			const char *p = line;
			while (isspace((unsigned char)*p))
				p++;
			if (!*p || *p == '#')
				continue;
			resolve(dir, p, out[n++], 512);
		} else if (!strcasecmp(e, ".gdi")) {
			/* First line is the track count, then
			 *   <track> <lba> <type> <sector size> <file> <offset>  */
			if (lineno == 1)
				continue;
			if (field(line, 4, name, sizeof(name)))
				resolve(dir, name, out[n++], 512);
		} else {
			/* .cue: FILE "name" BINARY; .toc: FILE or DATAFILE "name" */
			char key[16];
			if (!field(line, 0, key, sizeof(key)))
				continue;
			if (strcasecmp(key, "FILE") && strcasecmp(key, "DATAFILE"))
				continue;
			if (field(line, 1, name, sizeof(name)))
				resolve(dir, name, out[n++], 512);
		}
	}
	fclose(f);
	return n;
}
