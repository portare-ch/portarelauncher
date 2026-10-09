#include "idle.h"

#include <limits.h>

long long idle_sleep_at(long long last_act, int blank_min, int sleep_min,
                        long long retry_at)
{
	long long at = last_act + (blank_min + sleep_min) * 60000LL;
	return retry_at > at ? retry_at : at;
}

int idle_wait_ms(long long now, long long last_act, int blanked,
                 int blank_min, int sleep_min, long long retry_at)
{
	long long due = blanked
	              ? idle_sleep_at(last_act, blank_min, sleep_min, retry_at)
	              : last_act + blank_min * 60000LL;
	long long left = due - now;
	if (left <= 0)
		return 0;
	return left > INT_MAX ? INT_MAX : (int)left;
}
