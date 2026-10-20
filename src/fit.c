#include "fit.h"

struct fit fit_box(unsigned mode_w, unsigned mode_h,
                   unsigned base_w, unsigned base_h)
{
	struct fit f = { 0, 0, mode_w, mode_h, 0 };
	if (!base_w || !base_h)
		return f;
	unsigned zx = mode_w / base_w, zy = mode_h / base_h;
	f.zoom = zx < zy ? zx : zy;
	if (!f.zoom)
		return f;
	f.w = base_w * f.zoom;
	f.h = base_h * f.zoom;
	f.x = (mode_w - f.w) / 2;
	f.y = (mode_h - f.h) / 2;
	return f;
}
