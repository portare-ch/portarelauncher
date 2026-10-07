/* Pixels, for the few things the font cannot draw.
 *
 * The launcher is a text grid and stays one. PortScope needs a round ring
 * per stick with a dot that moves at the panel's resolution, and the shape
 * marks as thin outlines; these draw them straight into the framebuffer,
 * in whatever colour the palette gives. Everything is clipped to the
 * buffer, so a caller never has to.
 */
#ifndef PL_PIX_H
#define PL_PIX_H

#include <stdint.h>

struct pix {
	uint32_t *fb;
	unsigned pitch;      /* in pixels */
	unsigned w, h;
};

void pix_fill(struct pix *p, int x, int y, int w, int h, uint32_t c);
void pix_disc(struct pix *p, int cx, int cy, int r, uint32_t c);
/* r is the outer radius; the band is thick pixels wide inside it. */
void pix_ring(struct pix *p, int cx, int cy, int r, int thick, uint32_t c);
/* A segment thick pixels wide, square-ended. */
void pix_line(struct pix *p, int x0, int y0, int x1, int y1, int thick, uint32_t c);

/* The four face marks, outlines only, in a box 2s pixels across centred on
 * cx, cy. Named for their geometry. */
void pix_triangle(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);
void pix_square(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);
void pix_circle(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);
void pix_cross(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);

#endif
