/* Where the panel-sized screen goes on a display: the largest whole-number
 * zoom of it that fits the mode, centred, black around it. The panel itself
 * gets zoom 1 and the whole mode. */
#ifndef PL_FIT_H
#define PL_FIT_H

struct fit {
	unsigned x, y, w, h;   /* the screen's rectangle in the mode */
	unsigned zoom;         /* 0 when the mode is smaller than the screen */
};

/* A mode smaller than base_w x base_h gets zoom 0 and the whole mode. */
struct fit fit_box(unsigned mode_w, unsigned mode_h,
                   unsigned base_w, unsigned base_h);

#endif
