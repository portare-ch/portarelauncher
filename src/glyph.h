/* UTF-8 decoding and glyph widths (docs/japanese.md). Font lookup order:
 *   1. VGA/CP437, one cell;
 *   2. Unifont JIS X 0213, one or two cells according to the glyph;
 *   3. a drawable base letter, such as s for ș;
 *   4. ?, one cell.
 *
 * Combining marks are composed when supported, otherwise dropped.
 * Invalid UTF-8 bytes are read as Latin-1. */
#ifndef PL_GLYPH_H
#define PL_GLYPH_H

#include <stddef.h>
#include <stdint.h>

/* CP437 below GLYPH_UNI; Unifont codes are GLYPH_UNI + index. */
#define GLYPH_UNI 0x100

struct glyph {
	uint16_t code;
	uint8_t width;      /* cells: 1 or 2 */
};

/* Returns the next composed character and advances *s past its marks;
 * returns 0 at the end. */
uint32_t text_next(const char **s);

struct glyph glyph_of(uint32_t cp);

/* Returns 16 bitmap rows: one byte per row, or two when *wide is set. */
const unsigned char *glyph_bits(uint16_t code, int *wide);

/* The code point a Unifont glyph code stands for; 0 for a VGA one. */
uint32_t glyph_cp(uint16_t code);

int text_width(const char *s);

/* Bytes of the longest prefix fitting cols, keeping characters and marks whole. */
size_t text_fit(const char *s, int cols);

/* Returns the character containing column col, or the string end past it. */
const char *text_at_col(const char *s, int col);

/* Marquee stop: first character boundary at or past the overflow; 0 if s fits. */
int text_scroll_end(const char *s, int cols);

/* Composes in place, drops unsupported marks and converts Latin-1 to UTF-8
 * where it fits. Never lengthens the string. */
void text_compose(char *s);

#endif
