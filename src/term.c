#include "term.h"
#include "font8x16.h"
#include "glyph.h"
#include "ja26.h"
#include "pix.h"

#include <stdlib.h>
#include <string.h>

struct palette {
	const char *name;
	uint32_t c[ATTR_COUNT];
};

static const struct palette palettes[] = {
	/* Grey on black. The plain MS-DOS prompt: no hue at all, which is the
	 * least tiring to read and the most neutral behind amber text. */
	{ "grey", { 0x00000000, 0x00404040, 0x00808080, 0x00c0c0c0, 0x00ffffff } },

	/* Amber phosphor. Warm and period-correct, and on an OLED it reads
	 * closer to yellow than the hex value suggests. */
	{ "amber", { 0x00000000, 0x00663d00, 0x00b37400, 0x00ffb000, 0x00ffd98a } },

	/* Deeper amber, pushed towards orange to take the yellow out. */
	{ "orange", { 0x00000000, 0x00552200, 0x00994400, 0x00e06000, 0x00ff9040 } },

	/* Green phosphor, the VT220 end of the range. */
	{ "green", { 0x00000000, 0x00004400, 0x00008800, 0x0033dd33, 0x0099ff99 } },

	/* Cool white with a trace of blue, which reads as "screen" rather than
	 * as "paper" on a panel this contrasty. */
	{ "ice", { 0x00000000, 0x00303842, 0x005d6b7a, 0x00a8bccc, 0x00e8f2ff } },
};

#define N_PALETTES ((int)(sizeof(palettes) / sizeof(palettes[0])))

int term_palette_count(void) { return N_PALETTES; }
int term_palette(const struct term *t) { return t->pal; }

uint32_t term_color(const struct term *t, int attr)
{
	return palettes[t->pal].c[attr >= 0 && attr < ATTR_COUNT ? attr : ATTR_TEXT];
}

const char *term_set_palette(struct term *t, int idx)
{
	t->pal = ((idx % N_PALETTES) + N_PALETTES) % N_PALETTES;
	term_invalidate(t);      /* every cell has to be repainted */
	return palettes[t->pal].name;
}

int term_init(struct term *t, uint32_t *fb, unsigned pitch_px,
              unsigned w, unsigned h, unsigned scale)
{
	if (scale == 0)
		return -1;

	t->fb = fb;
	t->pitch_px = pitch_px;
	t->fb_w = w;
	t->fb_h = h;
	t->scale = scale;
	t->cols = w / (FONT_W * scale);
	t->rows = h / (FONT_H * scale);
	if (t->cols == 0 || t->rows == 0)
		return -1;

	/* Centre the grid so the leftover pixels are split rather than all
	 * landing on one edge. They stay black either way. */
	t->ox = (w - t->cols * FONT_W * scale) / 2;
	t->oy = (h - t->rows * FONT_H * scale) / 2;

	size_t n = (size_t)t->cols * t->rows;
	t->cur = calloc(n, sizeof(*t->cur));
	t->prev = calloc(n, sizeof(*t->prev));
	if (!t->cur || !t->prev) {
		term_free(t);
		return -1;
	}
	term_clear(t);
	term_invalidate(t);
	return 0;
}

void term_free(struct term *t)
{
	free(t->cur);
	free(t->prev);
	t->cur = t->prev = NULL;
}

void term_clear(struct term *t)
{
	size_t n = (size_t)t->cols * t->rows;
	for (size_t i = 0; i < n; i++)
		t->cur[i] = (struct cell){ ' ', ATTR_TEXT, HALF_NONE };
}

void term_invalidate(struct term *t)
{
	/* 0xff is not a value term_clear or the drawing helpers produce, so
	 * every cell compares unequal on the next flush. */
	memset(t->prev, 0xff, (size_t)t->cols * t->rows * sizeof(*t->prev));
}

/* Whatever cell x was half of, the other half is blank now. */
static void unpair(struct term *t, unsigned x, unsigned y)
{
	struct cell *row = &t->cur[(size_t)y * t->cols];
	if (row[x].half == HALF_LEFT && x + 1 < t->cols)
		row[x + 1] = (struct cell){ ' ', row[x].attr, HALF_NONE };
	else if (row[x].half == HALF_RIGHT && x > 0)
		row[x - 1] = (struct cell){ ' ', row[x].attr, HALF_NONE };
	row[x].half = HALF_NONE;
}

static void put_glyph(struct term *t, unsigned x, unsigned y, struct glyph g, int attr)
{
	if (x + g.width > t->cols || y >= t->rows)
		return;
	struct cell *row = &t->cur[(size_t)y * t->cols];
	unpair(t, x, y);
	if (g.width == 2) {
		unpair(t, x + 1, y);
		row[x] = (struct cell){ g.code, (uint8_t)attr, HALF_LEFT };
		row[x + 1] = (struct cell){ g.code, (uint8_t)attr, HALF_RIGHT };
	} else {
		row[x] = (struct cell){ g.code, (uint8_t)attr, HALF_NONE };
	}
}

void term_putc(struct term *t, unsigned x, unsigned y, unsigned char ch, int attr)
{
	put_glyph(t, x, y, (struct glyph){ ch, 1 }, attr);
}

unsigned term_puts(struct term *t, unsigned x, unsigned y, const char *s, int attr)
{
	unsigned x0 = x;
	uint32_t c;
	while ((c = text_next(&s))) {
		struct glyph g = glyph_of(c);
		if (x + g.width > t->cols)
			break;
		put_glyph(t, x, y, g, attr);
		x += g.width;
	}
	return x - x0;
}

void term_puts_right(struct term *t, unsigned x_end, unsigned y, const char *s, int attr)
{
	int w = text_width(s);
	if (w > (int)x_end)
		return;
	term_puts(t, x_end - (unsigned)w, y, s, attr);
}

void term_hline(struct term *t, unsigned y, unsigned char glyph, int attr)
{
	for (unsigned x = 1; x + 1 < t->cols; x++)
		term_putc(t, x, y, glyph, attr);
}

/* A shape mark across the whole cell: background first, then the outline.
 * Centred where the font's capitals are, rows 2 to 11 of 16, so a mark
 * sits on the line with the word beside it; half the size of the box it
 * fits in is 11 pixels at scale 3, two-pixel lines. */
static void draw_mark(struct term *t, unsigned px0, unsigned py0,
                      unsigned char ch, uint32_t fg, uint32_t bg)
{
	struct pix p = { t->fb, t->pitch_px, t->fb_w, t->fb_h };
	const int s = (int)t->scale;
	const int x = (int)px0, y = (int)py0;
	int size = 11 * s / 3, thick = (2 * s + 2) / 3;
	if (size < 3)
		size = 3;

	pix_fill(&p, x, y, FONT_W * s, FONT_H * s, bg);
	const int cx = x + FONT_W * s / 2, cy = y + 7 * s;
	switch (ch) {
	case G_MARK_CROSS:    pix_cross(&p, cx, cy, size, thick, fg);    break;
	case G_MARK_SQUARE:   pix_square(&p, cx, cy, size, thick, fg);   break;
	case G_MARK_TRIANGLE: pix_triangle(&p, cx, cy, size, thick, fg); break;
	default:              pix_circle(&p, cx, cy, size, thick, fg);   break;
	}
}

static int is_mark(uint16_t g)
{
	return g >= G_MARK_CROSS && g <= G_MARK_CIRCLE;
}

static uint32_t mix(uint32_t bg, uint32_t fg, unsigned a)   /* a: 0..255 */
{
	uint32_t out = 0;
	for (int sh = 0; sh <= 16; sh += 8) {
		int b = (int)(bg >> sh & 0xFF), f = (int)(fg >> sh & 0xFF);
		out |= (uint32_t)(b + (f - b) * (int)a / 255) << sh;
	}
	return out;
}

/* A two-cell glyph the PS2's way: 26 x 26 with 16 grey levels, scaled to
 * the 48 x 48 of two cells with bilinear filtering, the GS's
 * GS_FILTER_LINEAR, and blended between the cell's colour and black. */
static void draw_ja26(struct term *t, unsigned px0, unsigned py0,
                      const unsigned char *g, uint32_t fg, uint32_t bg)
{
	enum { OUT = 48 };
	static int i0[OUT], w1[OUT], ready;
	if (!ready) {
		/* The source position of each output pixel's centre, in 1/256. */
		for (int o = 0; o < OUT; o++) {
			int f = (2 * o + 1) * JA26_SIZE * 128 / OUT - 128;
			i0[o] = f >= 0 ? f / 256 : -1;
			w1[o] = f - i0[o] * 256;
		}
		ready = 1;
	}
#define L(y, x) ((y) < 0 || (y) >= JA26_SIZE || (x) < 0 || (x) >= JA26_SIZE ? 0 : \
	((x) & 1 ? g[(y) * 13 + (x) / 2] & 0x0F : g[(y) * 13 + (x) / 2] >> 4))
	for (int oy = 0; oy < OUT; oy++) {
		int y = i0[oy], wy = w1[oy];
		uint32_t *out = t->fb + (size_t)(py0 + (unsigned)oy) * t->pitch_px + px0;
		for (int ox = 0; ox < OUT; ox++) {
			int x = i0[ox], wx = w1[ox];
			int top = L(y, x) * (256 - wx) + L(y, x + 1) * wx;
			int bot = L(y + 1, x) * (256 - wx) + L(y + 1, x + 1) * wx;
			int a = top * (256 - wy) + bot * wy;    /* 0 .. 15 * 65536 */
			*out++ = a ? mix(bg, fg, (unsigned)(a / 3855)) : bg;
		}
	}
#undef L
}

/* A Unifont glyph from its left cell: 8 or 16 pixels by 16, at scale. */
static void draw_unifont(struct term *t, unsigned px0, unsigned py0,
                         const struct cell *c, uint32_t fg, uint32_t bg)
{
	int wide;
	const unsigned char *bits = glyph_bits(c->g, &wide);
	const unsigned s = t->scale;
	if (wide && s == 3) {
		const unsigned char *g = ja26_find(glyph_cp(c->g));
		if (g) {
			draw_ja26(t, px0, py0, g, fg, bg);
			return;
		}
	}
	const unsigned w = wide ? 16 : 8;
	for (unsigned gy = 0; gy < FONT_H; gy++) {
		unsigned row = wide ? (unsigned)(bits[gy * 2] << 8 | bits[gy * 2 + 1])
		                    : bits[gy];
		for (unsigned sy = 0; sy < s; sy++) {
			uint32_t *out = t->fb + (size_t)(py0 + gy * s + sy) * t->pitch_px + px0;
			for (unsigned gx = 0; gx < w; gx++) {
				uint32_t v = (row & (1u << (w - 1 - gx))) ? fg : bg;
				for (unsigned sx = 0; sx < s; sx++)
					*out++ = v;
			}
		}
	}
}

static void draw_cell(struct term *t, unsigned cx, unsigned cy, const struct cell *c)
{
	const unsigned char *glyph = &font8x16[(size_t)(c->g & 0xFF) * FONT_H];
	const uint32_t *pal = palettes[t->pal].c;
	const uint32_t fg = pal[c->attr < ATTR_COUNT ? c->attr : ATTR_TEXT];
	const uint32_t bg = pal[ATTR_BG];
	const unsigned s = t->scale;

	unsigned px0 = t->ox + cx * FONT_W * s;
	unsigned py0 = t->oy + cy * FONT_H * s;

	if (is_mark(c->g)) {
		draw_mark(t, px0, py0, (unsigned char)c->g, fg, bg);
		return;
	}
	if (c->g >= GLYPH_UNI) {
		draw_unifont(t, px0, py0, c, fg, bg);
		return;
	}

	for (unsigned gy = 0; gy < FONT_H; gy++) {
		unsigned char bits = glyph[gy];
		for (unsigned sy = 0; sy < s; sy++) {
			uint32_t *row = t->fb + (size_t)(py0 + gy * s + sy) * t->pitch_px + px0;
			for (unsigned gx = 0; gx < FONT_W; gx++) {
				uint32_t v = (bits & (0x80u >> gx)) ? fg : bg;
				for (unsigned sx = 0; sx < s; sx++)
					*row++ = v;
			}
		}
	}
}

static int same(const struct cell *a, const struct cell *b)
{
	return a->g == b->g && a->attr == b->attr && a->half == b->half;
}

void term_flush(struct term *t)
{
	size_t n = (size_t)t->cols * t->rows;
	for (size_t i = 0; i < n; i++) {
		if (same(&t->cur[i], &t->prev[i]))
			continue;
		/* A two-cell glyph is drawn whole from its left cell, whichever
		 * half changed. */
		size_t at = t->cur[i].half == HALF_RIGHT ? i - 1 : i;
		draw_cell(t, (unsigned)(at % t->cols), (unsigned)(at / t->cols), &t->cur[at]);
		t->prev[at] = t->cur[at];
		if (t->cur[at].half == HALF_LEFT)
			t->prev[at + 1] = t->cur[at + 1];
	}
}
