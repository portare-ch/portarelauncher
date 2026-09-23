#include "text.h"

#include <string.h>

void str_copy(char *dst, size_t dsz, const char *src)
{
	size_t n = strlen(src);
	if (dsz == 0)
		return;
	if (n >= dsz)
		n = dsz - 1;
	memmove(dst, src, n);
	dst[n] = '\0';
}

void strip_ansi(char *s)
{
	char *w = s;
	for (const char *r = s; *r; r++) {
		if (*r != '\033') {
			*w++ = *r;
			continue;
		}
		/* CSI: ESC [ ... final byte in 0x40-0x7e. Anything else after
		 * ESC is a two-byte sequence, so drop one more and carry on. */
		if (r[1] == '[') {
			r += 2;
			while (*r && (*r < 0x40 || *r > 0x7e))
				r++;
			if (!*r)
				break;
		} else if (r[1]) {
			r++;
		} else {
			break;
		}
	}
	*w = '\0';
}
