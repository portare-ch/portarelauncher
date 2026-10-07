#include "glyph.h"
#include "unifont.h"

#include <string.h>

/* One code point from UTF-8, or the byte as Latin-1 when it does not start
 * a well-formed sequence: overlong, surrogate, past U+10FFFF or cut short. */
static uint32_t decode(const unsigned char **p)
{
	const unsigned char *s = *p;
	uint32_t c = s[0];
	int n = 0;
	uint32_t min = 0;

	if (c < 0x80) {
		*p = s + 1;
		return c;
	}
	if ((c & 0xE0) == 0xC0)      { n = 1; c &= 0x1F; min = 0x80; }
	else if ((c & 0xF0) == 0xE0) { n = 2; c &= 0x0F; min = 0x800; }
	else if ((c & 0xF8) == 0xF0) { n = 3; c &= 0x07; min = 0x10000; }
	else {
		*p = s + 1;
		return s[0];
	}
	for (int i = 1; i <= n; i++) {
		if ((s[i] & 0xC0) != 0x80) {
			*p = s + 1;
			return s[0];
		}
		c = (c << 6) | (s[i] & 0x3F);
	}
	if (c < min || c > 0x10FFFF || (c >= 0xD800 && c < 0xE000)) {
		*p = s + 1;
		return s[0];
	}
	*p = s + 1 + n;
	return c;
}

static int is_combining(uint32_t cp)
{
	int lo = 0, hi = COMB_N - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (cp < comb_ranges[mid][0])
			hi = mid - 1;
		else if (cp > comb_ranges[mid][1])
			lo = mid + 1;
		else
			return 1;
	}
	return 0;
}

static uint32_t compose(uint32_t base, uint32_t mark)
{
	int lo = 0, hi = COMP_N - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		uint32_t b = comp_map[mid][0], m = comp_map[mid][1];
		if (base < b || (base == b && mark < m))
			hi = mid - 1;
		else if (base > b || mark > m)
			lo = mid + 1;
		else
			return comp_map[mid][2];
	}
	return 0;
}

uint32_t text_next(const char **s)
{
	const unsigned char *p = (const unsigned char *)*s;
	uint32_t c;

	/* Marks with nothing before them belong to nothing: dropped. */
	for (;;) {
		if (!*p) {
			*s = (const char *)p;
			return 0;
		}
		c = decode(&p);
		if (!is_combining(c))
			break;
	}
	while (*p) {
		const unsigned char *q = p;
		uint32_t m = decode(&q);
		if (!is_combining(m))
			break;
		uint32_t made = compose(c, m);
		if (made)
			c = made;
		p = q;            /* composed or dropped, it is used up */
	}
	*s = (const char *)p;
	return c;
}

static int find_uni(uint32_t cp)
{
	int lo = 0, hi = UNI_N - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		uint32_t k = uni_key[mid] & ~UNI_WIDE;
		if (cp < k)
			hi = mid - 1;
		else if (cp > k)
			lo = mid + 1;
		else
			return mid;
	}
	return -1;
}

static int find_vga(uint32_t cp)
{
	if (cp > 0xFFFF)
		return -1;
	int lo = 0, hi = VGA_N - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (cp < vga_map[mid][0])
			hi = mid - 1;
		else if (cp > vga_map[mid][0])
			lo = mid + 1;
		else
			return vga_map[mid][1];
	}
	return -1;
}

static uint32_t base_of(uint32_t cp)
{
	int lo = 0, hi = BASE_N - 1;
	while (lo <= hi) {
		int mid = (lo + hi) / 2;
		if (cp < base_map[mid][0])
			hi = mid - 1;
		else if (cp > base_map[mid][0])
			lo = mid + 1;
		else
			return base_map[mid][1];
	}
	return 0;
}

static int drawn(uint32_t cp, struct glyph *g)
{
	/* Below 0x20 is ours: CP437's symbols and the shape marks, which the
	 * program writes on purpose and text never holds. */
	if (cp < 0x7F) {
		*g = (struct glyph){ (uint16_t)cp, 1 };
		return 1;
	}
	int v = find_vga(cp);
	if (v >= 0) {
		*g = (struct glyph){ (uint16_t)v, 1 };
		return 1;
	}
	int u = find_uni(cp);
	if (u >= 0) {
		*g = (struct glyph){ (uint16_t)(GLYPH_UNI + u),
		                     (uint8_t)((uni_key[u] & UNI_WIDE) ? 2 : 1) };
		return 1;
	}
	return 0;
}

struct glyph glyph_of(uint32_t cp)
{
	struct glyph g;
	if (drawn(cp, &g))
		return g;
	uint32_t b = base_of(cp);
	if (b && drawn(b, &g))
		return g;
	return (struct glyph){ '?', 1 };
}

const unsigned char *glyph_bits(uint16_t code, int *wide)
{
	unsigned i = (unsigned)code - GLYPH_UNI;
	if (code < GLYPH_UNI || i >= UNI_N) {
		*wide = 0;
		return NULL;
	}
	*wide = (uni_key[i] & UNI_WIDE) != 0;
	return uni_bits[i];
}

int text_width(const char *s)
{
	int w = 0;
	uint32_t c;
	while ((c = text_next(&s)))
		w += glyph_of(c).width;
	return w;
}

size_t text_fit(const char *s, int cols)
{
	const char *start = s, *p = s;
	int w = 0;
	uint32_t c;
	for (;;) {
		const char *q = p;
		if (!(c = text_next(&q)))
			break;
		int cw = glyph_of(c).width;
		if (w + cw > cols)
			break;
		w += cw;
		p = q;
	}
	return (size_t)(p - start);
}

const char *text_at_col(const char *s, int col)
{
	const char *p = s;
	int w = 0;
	uint32_t c;
	for (;;) {
		const char *q = p;
		if (!(c = text_next(&q)))
			return p;
		int cw = glyph_of(c).width;
		if (w + cw > col)
			return p;
		w += cw;
		p = q;
	}
}

int text_scroll_end(const char *s, int cols)
{
	int over = text_width(s) - cols;
	if (over <= 0)
		return 0;
	int w = 0;
	uint32_t c;
	while (w < over && (c = text_next(&s)))
		w += glyph_of(c).width;
	return w;
}

static size_t put_utf8(char *out, uint32_t c)
{
	if (c < 0x80) {
		out[0] = (char)c;
		return 1;
	}
	if (c < 0x800) {
		out[0] = (char)(0xC0 | (c >> 6));
		out[1] = (char)(0x80 | (c & 0x3F));
		return 2;
	}
	if (c < 0x10000) {
		out[0] = (char)(0xE0 | (c >> 12));
		out[1] = (char)(0x80 | ((c >> 6) & 0x3F));
		out[2] = (char)(0x80 | (c & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (c >> 18));
	out[1] = (char)(0x80 | ((c >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((c >> 6) & 0x3F));
	out[3] = (char)(0x80 | (c & 0x3F));
	return 4;
}

void text_compose(char *s)
{
	const char *r = s;
	char *w = s;
	uint32_t c;
	while ((c = text_next(&r))) {
		char buf[4];
		size_t n = put_utf8(buf, c);
		/* A Latin-1 byte becomes two bytes of UTF-8, which would overrun
		 * what has not been read yet; it stays a byte, and still reads
		 * as Latin-1 next time. */
		if (w + n > r) {
			*w++ = (char)(c & 0xFF);
			continue;
		}
		memcpy(w, buf, n);
		w += n;
	}
	*w = '\0';
}
