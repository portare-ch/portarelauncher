/* Panel color profiles, applied through DRM CRTC color properties by kms.c.
 *
 * Profile format:
 *   # comment
 *   degamma R G B                              (256 lines, 0..65535, optional)
 *   ctm r.r r.g r.b g.r g.g g.b b.r b.g b.b   (out = M x in, row-major)
 *   lut R G B                                  (1024 lines, 0..65535)
 *
 * Profiles with degamma require a CRTC that exposes DEGAMMA_LUT. */
#ifndef PL_COLOR_H
#define PL_COLOR_H

#include <stddef.h>
#include <stdint.h>

#define COLOR_LUT_LEN 1024
#define COLOR_DEGAMMA_LEN 256

struct color_profile {
	double   ctm[9];
	uint16_t lut[COLOR_LUT_LEN][3];
	uint16_t degamma[COLOR_DEGAMMA_LEN][3];
	int      has_ctm, has_lut, has_degamma;
};

/* Returns 0, or -1 with a diagnostic on stderr. Requires a CTM or full LUT. */
int color_load(const char *path, struct color_profile *p);

/* DRM sign-magnitude S31.32 coefficient; bit 63 is the sign. */
uint64_t color_ctm_fixed(double v);

#endif
