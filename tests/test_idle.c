#include "check.h"
#include "idle.h"

#define MIN 60000LL

static void test_sleep_at(void)
{
	/* Blank time plus sleep time after the last press. */
	CHECK(idle_sleep_at(1000, 5, 10, 0) == 1000 + 15 * MIN);
	/* A declined request is asked again at the retry, not before. */
	CHECK(idle_sleep_at(1000, 5, 10, 1000 + 16 * MIN) == 1000 + 16 * MIN);
	/* A retry left over from before the last press is ignored. */
	CHECK(idle_sleep_at(30 * MIN, 5, 10, 20 * MIN) == 45 * MIN);
}

static void test_wait(void)
{
	/* Lit: the wait ends when the panel is due to go off. */
	CHECK_INT(idle_wait_ms(2 * MIN, 0, 0, 5, 10, 0), (int)(3 * MIN));
	/* Lit and past it: check now. */
	CHECK_INT(idle_wait_ms(6 * MIN, 0, 0, 5, 10, 0), 0);
	/* Dark: the wait ends at the sleep request, not at the blank. */
	CHECK_INT(idle_wait_ms(6 * MIN, 0, 1, 5, 10, 0), (int)(9 * MIN));
	/* Dark and due. */
	CHECK_INT(idle_wait_ms(15 * MIN, 0, 1, 5, 10, 0), 0);
	/* Dark, declined a moment ago: the next ask is a minute on. */
	CHECK_INT(idle_wait_ms(15 * MIN, 0, 1, 5, 10, 15 * MIN + IDLE_RETRY_MS),
	          (int)IDLE_RETRY_MS);
	/* A far deadline does not overflow the int input_wait() takes. */
	CHECK(idle_wait_ms(0, 1LL << 40, 1, 5, 10, 0) > 0);
}

int main(void)
{
	test_sleep_at();
	test_wait();
	return check_report("idle");
}
