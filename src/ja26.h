/* 26 x 26, 16-level Japanese glyphs from tools/mkja26.py, scaled with
 * bilinear filtering (docs/japanese.md). Falls back to Unifont without the file. */
#ifndef PL_JA26_H
#define PL_JA26_H

#include <stdint.h>

#define JA26_SIZE 26
#define JA26_BYTES (JA26_SIZE * JA26_SIZE / 2)

int ja26_load(const char *path);

/* The glyph for a code point, 26 rows of 13 bytes, or NULL. */
const unsigned char *ja26_find(uint32_t cp);

#endif
