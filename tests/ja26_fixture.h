/* A glyph file in ja26.bin's format, written for a test. */
#ifndef PL_JA26_FIXTURE_H
#define PL_JA26_FIXTURE_H

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ja26.h"

static inline void ja26_put32(FILE *f, uint32_t v)
{
	fputc((int)(v & 0xFF), f);
	fputc((int)(v >> 8 & 0xFF), f);
	fputc((int)(v >> 16 & 0xFF), f);
	fputc((int)(v >> 24 & 0xFF), f);
}

/* n glyphs for the code points in cps, glyph i from glyphs[i]; size is
 * what the header says; cut drops that many bytes from the end. */
static inline void ja26_fixture(const char *path, const char *magic, uint32_t n,
                                uint32_t size, const uint32_t *cps,
                                const unsigned char (*glyphs)[JA26_BYTES], size_t cut)
{
	char buf[16 + 16 * (4 + JA26_BYTES)];
	FILE *f = fmemopen(buf, sizeof(buf), "wb");
	fwrite(magic, 1, 8, f);
	ja26_put32(f, n);
	ja26_put32(f, size);
	for (uint32_t i = 0; i < n; i++)
		ja26_put32(f, cps[i]);
	for (uint32_t i = 0; i < n; i++)
		fwrite(glyphs[i], 1, JA26_BYTES, f);
	long len = ftell(f);
	fclose(f);
	FILE *o = fopen(path, "wb");
	fwrite(buf, 1, (size_t)len - cut, o);
	fclose(o);
}

#endif
