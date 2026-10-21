/* Which of a connector's modes to drive. The panel runs the fastest mode
 * at its own size. A display runs the largest mode of its own aspect that
 * is 120 Hz class, so VRR can show every frame of a 60 Hz game twice and
 * the mode never changes between games; 4K stays below that on this SoC.
 * Without one, its preferred mode. */
#ifndef PL_MODES_H
#define PL_MODES_H

struct mode_info {
	unsigned w, h;          /* active pixels */
	unsigned htotal, vtotal;
	unsigned clock_khz;
	int preferred;
	int interlaced;
};

/* The fastest refresh a 120 Hz class mode may round down to: 119.88 Hz
 * modes count, 100 Hz ones do not. */
#define MODES_MIN_MHZ 119500u

/* Refresh in mHz, 0 for a mode without totals. */
unsigned modes_refresh_mhz(const struct mode_info *m);

/* Index of the mode to drive, or -1 for an empty list. */
int modes_pick(const struct mode_info *m, int n, int internal);

#endif
