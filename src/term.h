/* A text grid over a linear framebuffer.
 *
 * The grid is the whole UI. Nothing else draws. Cells are compared against
 * the previous frame and only the ones that changed are written, because the
 * framebuffer is uncached write-combined memory where a full-screen blit
 * costs milliseconds and a menu that is not moving should cost nothing.
 */
#ifndef PL_TERM_H
#define PL_TERM_H

#include <stdint.h>

/* Monochrome on black. Fewer lit pixels is less power on an OLED, so the
 * ramp is used deliberately: most text sits at TEXT, structure at DIM, and
 * only the selected row is BRIGHT.
 *
 * Several are offered because a colour that reads well as a hex value does
 * not necessarily read well on this panel, and the only way to know is to
 * look at it. term_set_palette cycles them at runtime for that reason. */
enum {
	ATTR_BG = 0,   /* black - the emitter is off                     */
	ATTR_DIM,      /* rules, separators, disabled                    */
	ATTR_MID,      /* counts, key hints, secondary                   */
	ATTR_TEXT,     /* body text                                      */
	ATTR_BRIGHT,   /* the selected row, and only that                */
	ATTR_COUNT
};

/* CP437 box drawing, which is what the VGA font gives us. */
#define G_CARET    0x10  /* right-pointing triangle, the selection marker */
#define G_HLINE    0xC4  /* single horizontal                             */
#define G_HLINE_D  0xCD  /* double horizontal                             */

struct cell {
	unsigned char ch;
	unsigned char attr;
};

struct term {
	uint32_t *fb;        /* mapped framebuffer                       */
	unsigned pitch_px;   /* framebuffer stride in pixels             */
	unsigned fb_w, fb_h;
	unsigned scale;      /* integer glyph scale                      */
	unsigned cols, rows;
	unsigned ox, oy;     /* pixel origin, centring the grid          */
	struct cell *cur;
	struct cell *prev;
	int pal;
};

int  term_init(struct term *t, uint32_t *fb, unsigned pitch_px,
               unsigned w, unsigned h, unsigned scale);
void term_free(struct term *t);

void term_clear(struct term *t);
void term_putc(struct term *t, unsigned x, unsigned y, unsigned char ch, int attr);
void term_puts(struct term *t, unsigned x, unsigned y, const char *s, int attr);
/* Right-aligned so the last character lands on column x_end - 1. */
void term_puts_right(struct term *t, unsigned x_end, unsigned y, const char *s, int attr);
void term_hline(struct term *t, unsigned y, unsigned char glyph, int attr);

/* Switches the colour ramp. Wraps, and returns the name of the one now in
 * use so the UI can say which it is. */
const char *term_set_palette(struct term *t, int idx);
int term_palette_count(void);
int term_palette(const struct term *t);

/* Writes the cells that changed since the last flush. */
void term_flush(struct term *t);
/* Forgets what is on screen, so the next flush rewrites everything. Needed
 * after an emulator has had the display. */
void term_invalidate(struct term *t);

#endif
