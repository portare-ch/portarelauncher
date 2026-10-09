/* What the menu does when it is left alone: the panel goes off after the
 * blank time, and the device sleeps after the sleep time on top of that.
 * The sleep itself is power-handler's, under the same rules as the power
 * key; when it declines, the menu asks again a minute later. */
#ifndef PL_IDLE_H
#define PL_IDLE_H

#define IDLE_RETRY_MS 60000LL

/* When to ask for the device to sleep, in now_ms() time: blank_min plus
 * sleep_min after the last press, or retry_at if a declined request set it
 * later than that. */
long long idle_sleep_at(long long last_act, int blank_min, int sleep_min,
                        long long retry_at);

/* How long the menu's wait may last before it has to check again, for
 * input_wait(): until the panel goes off while it is lit, until the sleep
 * request while it is dark, 0 when that is already due. Capped to an int. */
int idle_wait_ms(long long now, long long last_act, int blanked,
                 int blank_min, int sleep_min, long long retry_at);

#endif
