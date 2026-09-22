#include "term.h"
#include "font8x16.h"

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
	for (size_t i = 0; i < n; i++) {
		t->cur[i].ch = ' ';
		t->cur[i].attr = ATTR_TEXT;
	}
}

void term_invalidate(struct term *t)
{
	/* 0xff is not a value term_clear or the drawing helpers produce, so
	 * every cell compares unequal on the next flush. */
	memset(t->prev, 0xff, (size_t)t->cols * t->rows * sizeof(*t->prev));
}

void term_putc(struct term *t, unsigned x, unsigned y, unsigned char ch, int attr)
{
	if (x >= t->cols || y >= t->rows)
		return;
	struct cell *c = &t->cur[(size_t)y * t->cols + x];
	c->ch = ch;
	c->attr = (unsigned char)attr;
}

void term_puts(struct term *t, unsigned x, unsigned y, const char *s, int attr)
{
	for (; *s && x < t->cols; s++, x++)
		term_putc(t, x, y, (unsigned char)*s, attr);
}

void term_puts_right(struct term *t, unsigned x_end, unsigned y, const char *s, int attr)
{
	size_t len = strlen(s);
	if (len > x_end)
		return;
	term_puts(t, (unsigned)(x_end - len), y, s, attr);
}

void term_hline(struct term *t, unsigned y, unsigned char glyph, int attr)
{
	for (unsigned x = 1; x + 1 < t->cols; x++)
		term_putc(t, x, y, glyph, attr);
}

static void draw_cell(struct term *t, unsigned cx, unsigned cy, const struct cell *c)
{
	const unsigned char *glyph = &font8x16[(size_t)c->ch * FONT_H];
	const uint32_t *pal = palettes[t->pal].c;
	const uint32_t fg = pal[c->attr < ATTR_COUNT ? c->attr : ATTR_TEXT];
	const uint32_t bg = pal[ATTR_BG];
	const unsigned s = t->scale;

	unsigned px0 = t->ox + cx * FONT_W * s;
	unsigned py0 = t->oy + cy * FONT_H * s;

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

void term_flush(struct term *t)
{
	size_t n = (size_t)t->cols * t->rows;
	for (size_t i = 0; i < n; i++) {
		if (t->cur[i].ch == t->prev[i].ch && t->cur[i].attr == t->prev[i].attr)
			continue;
		draw_cell(t, (unsigned)(i % t->cols), (unsigned)(i / t->cols), &t->cur[i]);
		t->prev[i] = t->cur[i];
	}
}
