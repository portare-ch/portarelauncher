#include "modes.h"

unsigned modes_refresh_mhz(const struct mode_info *m)
{
	unsigned long long px = (unsigned long long)m->htotal * m->vtotal;
	if (!px)
		return 0;
	return (unsigned)((unsigned long long)m->clock_khz * 1000000ull / px);
}

static int preferred(const struct mode_info *m, int n)
{
	for (int i = 0; i < n; i++)
		if (m[i].preferred)
			return i;
	return 0;
}

int modes_pick(const struct mode_info *m, int n, int internal)
{
	if (n <= 0)
		return -1;
	const int p = preferred(m, n);
	int best = -1;

	for (int i = 0; i < n; i++) {
		if (m[i].interlaced)
			continue;
		if (internal) {
			if (m[i].w != m[p].w || m[i].h != m[p].h)
				continue;
			if (best < 0 || m[i].clock_khz > m[best].clock_khz)
				best = i;
			continue;
		}
		/* The display's own aspect, cross-multiplied. */
		if ((unsigned long long)m[i].w * m[p].h !=
		    (unsigned long long)m[i].h * m[p].w)
			continue;
		if (modes_refresh_mhz(&m[i]) < MODES_MIN_MHZ)
			continue;
		if (best < 0) {
			best = i;
			continue;
		}
		unsigned long long a = (unsigned long long)m[i].w * m[i].h;
		unsigned long long b = (unsigned long long)m[best].w * m[best].h;
		if (a > b || (a == b && modes_refresh_mhz(&m[i]) > modes_refresh_mhz(&m[best])))
			best = i;
	}
	return best >= 0 ? best : p;
}
