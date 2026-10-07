/* Text beyond ASCII: what a character is drawn with, and how wide it is.
 *
 * Strings are UTF-8. A character is drawn by the first of these that has
 * it, and that decides its width; there is no Unicode width table:
 *
 *   1. the VGA font, font8x16.h: ASCII and the rest of CP437, one cell;
 *   2. Unifont (unifont.c): JIS X 0213, one cell or two by the glyph;
 *   3. the base letter of a composed character, if 1 or 2 draws it: s for ș;
 *   4. ? in one cell.
 *
 * Decoding composes as it goes: a base followed by a combining mark is the
 * composed character when a font has it, and a mark left over is dropped.
 * A file copied from a Mac is named e + U+0301; it reads as é. A byte that
 * is not UTF-8 is taken as Latin-1, so a Latin-1 name still reads.
 * docs/japanese.md has the reasoning.
 */
#ifndef PL_GLYPH_H
#define PL_GLYPH_H

#include <stddef.h>
#include <stdint.h>

/* A cell's glyph: below GLYPH_UNI a CP437 code in the VGA font, from it
 * GLYPH_UNI plus an index into Unifont. */
#define GLYPH_UNI 0x100

struct glyph {
	uint16_t code;
	uint8_t width;      /* cells: 1 or 2 */
};

/* The next character of *s, composed, with *s moved past it and any marks
 * it absorbed; 0 at the end. */
uint32_t text_next(const char **s);

struct glyph glyph_of(uint32_t cp);

/* The 16 rows of a Unifont glyph, two bytes a row when it is wide and one
 * when it is not; *wide says which. */
const unsigned char *glyph_bits(uint16_t code, int *wide);

/* The code point a Unifont glyph code stands for; 0 for a VGA one. */
uint32_t glyph_cp(uint16_t code);

/* Columns s takes. */
int text_width(const char *s);

/* Bytes of the longest start of s that fits in cols columns; never half a
 * character, never a base without its marks. */
size_t text_fit(const char *s, int cols);

/* The character column col falls in, drawn whole from there: a marquee
 * col columns in. Past the end, the end. */
const char *text_at_col(const char *s, int col);

/* Where a marquee over s in a field cols wide stops: the first character
 * boundary at or past the overflow, so the end is in view. 0 when s fits. */
int text_scroll_end(const char *s, int cols);

/* Rewrites s in place as it reads: composed, marks that compose with
 * nothing dropped, Latin-1 bytes as UTF-8 where that fits. Never longer
 * than it was. */
void text_compose(char *s);

#endif
