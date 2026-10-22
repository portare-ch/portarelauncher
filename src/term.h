/* Text grid over a linear framebuffer. Only changed cells are written
 * to keep writes to uncached framebuffer memory small. */
#ifndef PL_TERM_H
#define PL_TERM_H

#include <stdint.h>

/* Monochrome ramps on black: body text at TEXT, structure at DIM,
 * selection at BRIGHT. */
enum {
	ATTR_BG = 0,   /* black - the emitter is off */
	ATTR_DIM,      /* rules, separators, disabled */
	ATTR_MID,      /* counts, key hints, secondary */
	ATTR_TEXT,     /* body text */
	ATTR_BRIGHT,   /* the selected row, and only that */
	ATTR_COUNT
};

/* CP437 box drawing, which is what the VGA font gives us. */
#define G_CARET    0x10  /* right-pointing triangle, the selection marker */
#define G_HLINE    0xC4  /* single horizontal */
#define G_HLINE_D  0xCD  /* double horizontal */

/* Face-button control codes, rendered as outlines by pix.h rather than
 * VGA glyphs. External text cannot produce these codes. */
#define G_MARK_CROSS     0x1C
#define G_MARK_SQUARE    0x1D
#define G_MARK_TRIANGLE  0x1E
#define G_MARK_CIRCLE    0x1F

/* Two-cell glyphs store the same code in both halves; writing over
 * either half clears the other. */
enum { HALF_NONE = 0, HALF_LEFT, HALF_RIGHT };

struct cell {
	uint16_t g;
	uint8_t attr;
	uint8_t half;
};

struct term {
	uint32_t *fb;        /* mapped framebuffer */
	unsigned pitch_px;   /* framebuffer stride in pixels */
	unsigned fb_w, fb_h;
	unsigned scale;      /* integer glyph scale */
	unsigned cols, rows;
	int base_x, base_y;  /* pixel origin centring the grid */
	int ox, oy;          /* pixel origin now: base plus the term_shift offset */
	uint32_t *scratch;   /* a two-cell glyph, for cells the box clips */
	struct cell *cur;
	struct cell *prev;
	int pal;
	int dim;             /* every colour at half, until term_set_dim(t, 0) */
	uint32_t dimmed[ATTR_COUNT];
};

int  term_init(struct term *t, uint32_t *fb, unsigned pitch_px,
               unsigned w, unsigned h, unsigned scale);
void term_free(struct term *t);

void term_clear(struct term *t);
/* One CP437 glyph: the box drawing, the caret, the shape marks. */
void term_putc(struct term *t, unsigned x, unsigned y, unsigned char ch, int attr);
/* Draws UTF-8, stopping before a character would cross the right edge.
 * Returns the number of columns drawn. */
unsigned term_puts(struct term *t, unsigned x, unsigned y, const char *s, int attr);
/* Right-aligned by width, so the last character ends on column x_end - 1. */
void term_puts_right(struct term *t, unsigned x_end, unsigned y, const char *s, int attr);
void term_hline(struct term *t, unsigned y, unsigned char glyph, int attr);

/* Switches palette with wrapping; returns its name. */
const char *term_set_palette(struct term *t, int idx);
int term_palette_count(void);
int term_palette(const struct term *t);
/* Current palette color for pixel drawing beside the grid. */
uint32_t term_color(const struct term *t, int attr);
/* Draws every colour at half its value, a fifth of the light: the menu
 * dims before the screen goes off. Returns 1 when that changed, and the
 * next flush repaints everything. */
int term_set_dim(struct term *t, int on);

/* Moves the grid dx, dy pixels from its centred place, so static text does
 * not light the same OLED pixels for hours. Clears what the grid leaves and
 * repaints it on the next flush; cells past the box's edge are clipped. */
void term_shift(struct term *t, int dx, int dy);

/* Writes the cells that changed since the last flush. */
void term_flush(struct term *t);
/* Forces a full repaint on the next flush, e.g. after an emulator exits. */
void term_invalidate(struct term *t);

#endif
