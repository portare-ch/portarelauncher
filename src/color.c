#include "color.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

uint64_t color_ctm_fixed(double v)
{
	uint64_t sign = 0;
	if (v < 0) {
		sign = 1ull << 63;
		v = -v;
	}
	/* 31 integer bits is more than any correction needs; clamp rather
	 * than wrap if a file is nonsense. */
	if (v > 2147483647.0)
		v = 2147483647.0;
	/* Rounded by hand: llround would pull in libm, and this program links
	 * against libc and libdrm and nothing else. v is non-negative here. */
	return sign | (uint64_t)(v * 4294967296.0 + 0.5);
}

int color_load(const char *path, struct color_profile *p)
{
	memset(p, 0, sizeof(*p));

	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "color: open %s: %s\n", path, strerror(errno));
		return -1;
	}

	char line[256];
	int lut_n = 0, dg_n = 0, lineno = 0, bad = 0;
	while (fgets(line, sizeof(line), f)) {
		lineno++;
		char *s = line;
		while (*s == ' ' || *s == '\t')
			s++;
		if (*s == '#' || *s == '\n' || *s == '\0')
			continue;

		if (strncmp(s, "ctm", 3) == 0) {
			double m[9];
			if (sscanf(s + 3, "%lf %lf %lf %lf %lf %lf %lf %lf %lf",
			           &m[0], &m[1], &m[2], &m[3], &m[4], &m[5],
			           &m[6], &m[7], &m[8]) != 9) {
				bad = lineno;
				break;
			}
			memcpy(p->ctm, m, sizeof(m));
			p->has_ctm = 1;
		} else if (strncmp(s, "degamma", 7) == 0) {
			unsigned r, g, b;
			if (dg_n >= COLOR_DEGAMMA_LEN ||
			    sscanf(s + 7, "%u %u %u", &r, &g, &b) != 3 ||
			    r > 65535 || g > 65535 || b > 65535) {
				bad = lineno;
				break;
			}
			p->degamma[dg_n][0] = (uint16_t)r;
			p->degamma[dg_n][1] = (uint16_t)g;
			p->degamma[dg_n][2] = (uint16_t)b;
			dg_n++;
		} else if (strncmp(s, "lut", 3) == 0) {
			unsigned r, g, b;
			if (lut_n >= COLOR_LUT_LEN ||
			    sscanf(s + 3, "%u %u %u", &r, &g, &b) != 3 ||
			    r > 65535 || g > 65535 || b > 65535) {
				bad = lineno;
				break;
			}
			p->lut[lut_n][0] = (uint16_t)r;
			p->lut[lut_n][1] = (uint16_t)g;
			p->lut[lut_n][2] = (uint16_t)b;
			lut_n++;
		} else {
			bad = lineno;
			break;
		}
	}
	fclose(f);

	if (bad) {
		fprintf(stderr, "color: %s:%d: cannot read this line\n", path, bad);
		return -1;
	}
	if (lut_n == COLOR_LUT_LEN)
		p->has_lut = 1;
	else if (lut_n) {
		fprintf(stderr, "color: %s: %d lut lines, need %d\n", path, lut_n,
		        COLOR_LUT_LEN);
		return -1;
	}
	if (dg_n == COLOR_DEGAMMA_LEN)
		p->has_degamma = 1;
	else if (dg_n) {
		fprintf(stderr, "color: %s: %d degamma lines, need %d\n", path, dg_n,
		        COLOR_DEGAMMA_LEN);
		return -1;
	}
	if (!p->has_ctm && !p->has_lut) {
		fprintf(stderr, "color: %s: no ctm and no lut\n", path);
		return -1;
	}
	return 0;
}
