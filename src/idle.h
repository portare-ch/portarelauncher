/* Blank after the idle timeout, then request sleep through power-handler.
 * Declined sleep requests retry after one minute. */
#ifndef PL_IDLE_H
#define PL_IDLE_H

#define IDLE_RETRY_MS 60000LL

/* Sleep deadline in now_ms() time: last activity + blank + sleep,
 * or retry_at if later. */
long long idle_sleep_at(long long last_act, int blank_min, int sleep_min,
                        long long retry_at);

/* When the lit menu dims: halfway to the screen going off. */
long long idle_dim_at(long long last_act, int blank_min);

/* Milliseconds until blanking (lit) or sleep (dark), for input_wait().
 * Returns 0 if due; capped to int. */
int idle_wait_ms(long long now, long long last_act, int blanked,
                 int blank_min, int sleep_min, long long retry_at);

#endif
