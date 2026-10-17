/* Clipped framebuffer primitives for stick indicators and button outlines. */
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
/* A segment thick pixels wide, with round caps. */
void pix_line(struct pix *p, int x0, int y0, int x1, int y1, int thick, uint32_t c);

/* Face-button outlines in a 2s-pixel box centered at cx, cy. */
void pix_triangle(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);
void pix_square(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);
void pix_circle(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);
void pix_cross(struct pix *p, int cx, int cy, int s, int thick, uint32_t c);

#endif
