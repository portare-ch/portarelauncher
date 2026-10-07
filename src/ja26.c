#include "ja26.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *data;
static uint32_t count;
static const unsigned char *cps, *bits;

static uint32_t u32_at(const unsigned char *p)
{
	return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
	       (uint32_t)p[3] << 24;
}

int ja26_load(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return -1;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	unsigned char *d = size > 16 ? malloc((size_t)size) : NULL;
	if (!d || fread(d, 1, (size_t)size, f) != (size_t)size) {
		free(d);
		fclose(f);
		return -1;
	}
	fclose(f);

	uint32_t n = u32_at(d + 8);
	if (memcmp(d, "PLJA26\0\1", 8) != 0 || u32_at(d + 12) != JA26_SIZE ||
	    (uint64_t)size != 16 + (uint64_t)n * (4 + JA26_BYTES)) {
		fprintf(stderr, "ja26: %s is not a glyph file this program reads\n", path);
		free(d);
		return -1;
	}
	free(data);
	data = d;
	count = n;
	cps = d + 16;
	bits = cps + (size_t)n * 4;
	return 0;
}

const unsigned char *ja26_find(uint32_t cp)
{
	int lo = 0, hi = (int)count - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		uint32_t k = u32_at(cps + (size_t)mid * 4);
		if (cp < k)
			hi = mid - 1;
		else if (cp > k)
			lo = mid + 1;
		else
			return bits + (size_t)mid * JA26_BYTES;
	}
	return NULL;
}
