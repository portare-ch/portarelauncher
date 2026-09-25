/* Color profiles for the panel: a 3x3 matrix and a gamma table, applied to
 * the display controller's own color blocks (the DRM CRTC properties CTM and
 * GAMMA_LUT), so they correct everything scanned out - the launcher, every
 * emulator, mpv - without any of them knowing.
 *
 * A profile is a text file:
 *
 *   # comment
 *   ctm  r.r r.g r.b  g.r g.g g.b  b.r b.g b.b     (out = M x in, row-major)
 *   lut  R G B                                     (1024 lines, 0..65535)
 *
 * Parsing and the fixed-point conversion live here, with no DRM in sight, so
 * they can be tested on the laptop. kms.c does the property writes.
 */
#ifndef PL_COLOR_H
#define PL_COLOR_H

#include <stddef.h>
#include <stdint.h>

#define COLOR_LUT_LEN 1024

struct color_profile {
	double   ctm[9];
	uint16_t lut[COLOR_LUT_LEN][3];
	int      has_ctm, has_lut;
};

/* Reads a profile file. Returns 0, or -1 with a message on stderr. A file
 * with neither a ctm nor a full lut is an error. */
int color_load(const char *path, struct color_profile *p);

/* One CTM coefficient as DRM wants it: sign-magnitude S31.32, the sign in
 * bit 63. */
uint64_t color_ctm_fixed(double v);

#endif
