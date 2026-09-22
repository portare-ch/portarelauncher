#include "settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char *slurp(const char *path, size_t *len)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (n < 0) {
		fclose(f);
		return NULL;
	}

	char *buf = malloc((size_t)n + 1);
	if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
		buf[n] = '\0';
		if (len)
			*len = (size_t)n;
	} else {
		free(buf);
		buf = NULL;
	}
	fclose(f);
	return buf;
}

int settings_get(const char *path, const char *key, char *out, size_t osz)
{
	char *text = slurp(path, NULL);
	if (!text)
		return 0;

	char want[160];
	int n = snprintf(want, sizeof(want), "%s=", key);
	if (n < 0 || (size_t)n >= sizeof(want)) {
		free(text);
		return 0;
	}

	const char *p;
	if (strncmp(text, want, (size_t)n) == 0) {
		p = text + n;               /* first line, no newline in front */
	} else {
		char nl[162];
		snprintf(nl, sizeof(nl), "\n%s", want);
		p = strstr(text, nl);
		if (!p) {
			free(text);
			return 0;
		}
		p += strlen(nl);
	}

	size_t len = strcspn(p, "\n");
	while (len && isspace((unsigned char)p[len - 1]))
		len--;
	if (len >= osz)
		len = osz - 1;
	memcpy(out, p, len);
	out[len] = '\0';

	free(text);
	return out[0] != '\0';
}

int settings_set(const char *path, const char *key, const char *value)
{
	size_t len = 0;
	char *text = slurp(path, &len);
	if (!text) {
		text = calloc(1, 1);
		if (!text)
			return -1;
	}

	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s.pl-tmp", path);

	FILE *out = fopen(tmp, "wb");
	if (!out) {
		free(text);
		return -1;
	}

	char want[160];
	snprintf(want, sizeof(want), "%s=", key);
	size_t wlen = strlen(want);

	int written = 0;
	const char *line = text;
	while (*line) {
		size_t n = strcspn(line, "\n");
		int has_nl = line[n] == '\n';

		if (!written && n >= wlen && strncmp(line, want, wlen) == 0) {
			fprintf(out, "%s%s\n", want, value);
			written = 1;
		} else {
			fwrite(line, 1, n, out);
			fputc('\n', out);
		}
		line += n + (has_nl ? 1 : 0);
	}

	if (!written)
		fprintf(out, "%s%s\n", want, value);

	int ok = (fflush(out) == 0);
	fclose(out);
	free(text);

	if (!ok || rename(tmp, path) < 0) {
		unlink(tmp);
		return -1;
	}
	return 0;
}
