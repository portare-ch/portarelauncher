#include "pix.h"

#include <stddef.h>

static void put(struct pix *p, int x, int y, uint32_t c)
{
	if (x < 0 || y < 0 || (unsigned)x >= p->w || (unsigned)y >= p->h)
		return;
	p->fb[(size_t)y * p->pitch + (size_t)x] = c;
}

void pix_fill(struct pix *p, int x, int y, int w, int h, uint32_t c)
{
	for (int j = y; j < y + h; j++)
		for (int i = x; i < x + w; i++)
			put(p, i, j, c);
}

/* Both round shapes test every pixel of the bounding box against the
 * squared radius. The boxes are a few hundred pixels across at most and
 * only the pixels that are set touch the framebuffer, which is the slow
 * part; a scan-line algorithm would save nothing that shows. */
void pix_disc(struct pix *p, int cx, int cy, int r, uint32_t c)
{
	for (int dy = -r; dy <= r; dy++)
		for (int dx = -r; dx <= r; dx++)
			if (dx * dx + dy * dy < r * r)
				put(p, cx + dx, cy + dy, c);
}

void pix_ring(struct pix *p, int cx, int cy, int r, int thick, uint32_t c)
{
	int ri = r - thick;
	if (ri < 0)
		ri = 0;
	for (int dy = -r; dy <= r; dy++)
		for (int dx = -r; dx <= r; dx++) {
			int d2 = dx * dx + dy * dy;
			if (d2 < r * r && d2 >= ri * ri)
				put(p, cx + dx, cy + dy, c);
		}
}

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

/* Every pixel of the segment's bounding box, widened by the thickness,
 * whose distance to the segment is under half the thickness. Exact for any
 * angle, which the marks need: a diagonal drawn by stepping looks thinner
 * than a vertical one of the same nominal width. */
void pix_line(struct pix *p, int x0, int y0, int x1, int y1, int thick, uint32_t c)
{
	int half = thick / 2;
	int bx0 = imin(x0, x1) - half - 1, bx1 = imax(x0, x1) + half + 1;
	int by0 = imin(y0, y1) - half - 1, by1 = imax(y0, y1) + half + 1;
	long vx = x1 - x0, vy = y1 - y0;
	long len2 = vx * vx + vy * vy;
	/* Pixel centres sit at .5; comparing squared distances scaled by 4
	 * keeps everything in integers. */
	long limit2 = (long)thick * thick;   /* (2 * thick/2)^2 */

	for (int y = by0; y <= by1; y++)
		for (int x = bx0; x <= bx1; x++) {
			long px = 2L * x + 1 - 2L * x0, py = 2L * y + 1 - 2L * y0;
			long t_num = px * vx + py * vy;    /* projection, scaled by len2 */
			long dx, dy;
			if (len2 == 0) {
				dx = px; dy = py;
			} else if (t_num <= 0) {
				dx = px; dy = py;
			} else if (t_num >= 2 * len2) {
				dx = px - 2 * vx; dy = py - 2 * vy;
			} else {
				/* Perpendicular distance via the cross product. */
				long cross = px * vy - py * vx;
				if (cross * cross <= limit2 * len2)
					put(p, x, y, c);
				continue;
			}
			if (dx * dx + dy * dy <= limit2)
				put(p, x, y, c);
		}
}

void pix_triangle(struct pix *p, int cx, int cy, int s, int thick, uint32_t c)
{
	/* Apex up, base a little below centre so the mass sits where a
	 * letter's would. */
	int ax = cx, ay = cy - s;
	int bx = cx - s, by = cy + s * 3 / 4;
	int dx = cx + s, dy = by;
	pix_line(p, ax, ay, bx, by, thick, c);
	pix_line(p, bx, by, dx, dy, thick, c);
	pix_line(p, dx, dy, ax, ay, thick, c);
}

void pix_square(struct pix *p, int cx, int cy, int s, int thick, uint32_t c)
{
	int h = s * 4 / 5;
	pix_line(p, cx - h, cy - h, cx + h, cy - h, thick, c);
	pix_line(p, cx + h, cy - h, cx + h, cy + h, thick, c);
	pix_line(p, cx + h, cy + h, cx - h, cy + h, thick, c);
	pix_line(p, cx - h, cy + h, cx - h, cy - h, thick, c);
}

void pix_circle(struct pix *p, int cx, int cy, int s, int thick, uint32_t c)
{
	pix_ring(p, cx, cy, s, thick, c);
}

void pix_cross(struct pix *p, int cx, int cy, int s, int thick, uint32_t c)
{
	int h = s * 4 / 5;
	pix_line(p, cx - h, cy - h, cx + h, cy + h, thick, c);
	pix_line(p, cx - h, cy + h, cx + h, cy - h, thick, c);
}
