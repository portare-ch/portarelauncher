/* Monotonic deadlines shared by the UI and process runner. */
#ifndef PL_TIMEUTIL_H
#define PL_TIMEUTIL_H

#include <time.h>

static inline long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

#endif
