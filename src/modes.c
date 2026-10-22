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

/* 2 for 144 Hz class, 1 for 120 Hz class, 0 for any other rate. */
static int rate_rank(unsigned mhz)
{
	if (mhz >= 143500 && mhz < 144500)
		return 2;
	if (mhz < 120500)
		return 1;
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
		unsigned hz = modes_refresh_mhz(&m[i]);
		if (m[i].h > MODES_MAX_LINES || hz < MODES_MIN_MHZ)
			continue;
		if (best < 0) {
			best = i;
			continue;
		}
		unsigned long long a = (unsigned long long)m[i].w * m[i].h;
		unsigned long long b = (unsigned long long)m[best].w * m[best].h;
		unsigned bhz = modes_refresh_mhz(&m[best]);
		int r = rate_rank(hz), br = rate_rank(bhz);
		/* Among other rates, the one nearest 120 Hz. */
		if (a > b || (a == b && (r > br || (r == br && r == 0 && hz < bhz))))
			best = i;
	}
	return best >= 0 ? best : p;
}
