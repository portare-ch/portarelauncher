#include "orbit.h"

static int triangle(long long minute, int amp)
{
	if (amp <= 0)
		return 0;
	const long long period = 4LL * amp;
	const int p = (int)(((minute % period) + period) % period);
	if (p <= amp)
		return p;
	if (p <= 3 * amp)
		return 2 * amp - p;
	return p - 4 * amp;
}

void orbit_offset(long long minute, int amp_x, int amp_y, int *dx, int *dy)
{
	*dx = triangle(minute, amp_x);
	*dy = triangle(minute, amp_y);
}
