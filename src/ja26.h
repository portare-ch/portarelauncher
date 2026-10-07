/* The kana and kanji as the PS2 drew its system font: 26 x 26 pixels with
 * 16 grey levels, scaled with bilinear filtering (docs/japanese.md). Read
 * from a file tools/mkja26.py makes; without it, Unifont's 16 x 16 glyphs
 * are drawn as before. */
#ifndef PL_JA26_H
#define PL_JA26_H

#include <stdint.h>

#define JA26_SIZE 26
#define JA26_BYTES (JA26_SIZE * JA26_SIZE / 2)

int ja26_load(const char *path);

/* The glyph for a code point, 26 rows of 13 bytes, or NULL. */
const unsigned char *ja26_find(uint32_t cp);

#endif
