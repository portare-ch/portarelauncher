#include "check.h"
#include "quit.h"

#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#define PRESS   1
#define RELEASE 0
#define REPEAT  2

static void test_combo(void)
{
	struct quit_combo q;
	quit_reset(&q);

	/* Home held, then START: that press completes it. */
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, PRESS), 0);
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_START, PRESS), 1);
	quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_START, RELEASE);
	quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, RELEASE);

	/* Either order. */
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_START, PRESS), 0);
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, PRESS), 1);

	/* Autorepeat while both are held is not another press. */
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, REPEAT), 0);
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_START, REPEAT), 0);
	quit_reset(&q);

	/* Released in between: START alone is just START. */
	quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, PRESS);
	quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, RELEASE);
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_START, PRESS), 0);
	quit_reset(&q);

	/* Home on one pad and START on another is two people. */
	quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, PRESS);
	CHECK_INT(quit_feed(&q, 1, QUIT_EV_KEY, QUIT_BTN_START, PRESS), 0);
	quit_reset(&q);

	/* Other keys, other event types and bad devices do nothing. */
	quit_feed(&q, 0, QUIT_EV_KEY, QUIT_BTN_MODE, PRESS);
	CHECK_INT(quit_feed(&q, 0, QUIT_EV_KEY, 0x13a /* SELECT */, PRESS), 0);
	CHECK_INT(quit_feed(&q, 0, 0x03 /* EV_ABS */, QUIT_BTN_START, PRESS), 0);
	CHECK_INT(quit_feed(&q, -1, QUIT_EV_KEY, QUIT_BTN_START, PRESS), 0);
	CHECK_INT(quit_feed(&q, QUIT_MAX_DEV, QUIT_EV_KEY, QUIT_BTN_START, PRESS), 0);
}

static void test_stat(void)
{
	int pg = 0, pp = 0;
	CHECK_INT(quit_stat_pgrp("1234 (retroarch) S 1200 1200 1200 0 -1", &pg, &pp), 0);
	CHECK_INT(pp, 1200);
	CHECK_INT(pg, 1200);

	/* A name with spaces and parentheses: counted from the last ')'. */
	CHECK_INT(quit_stat_pgrp("77 (a) b (c)) R 5 42 42 0", &pg, &pp), 0);
	CHECK_INT(pp, 5);
	CHECK_INT(pg, 42);

	CHECK_INT(quit_stat_pgrp("garbage", &pg, &pp), -1);
	CHECK_INT(quit_stat_pgrp("1 (x)", &pg, &pp), -1);
}

/* Linux only: /proc is how the group is found. A leader that stands in for
 * runemu.sh and a member that stands in for the emulator; the member is
 * signalled, the leader is left to finish. */
static void test_signal_group(void)
{
	FILE *probe = fopen("/proc/self/stat", "r");
	if (!probe) {
		printf("quit       (no /proc here: group signalling not tested)\n");
		return;
	}
	fclose(probe);

	int pipefd[2];
	CHECK_INT(pipe(pipefd), 0);
	pid_t leader = fork();
	if (leader == 0) {
		setpgid(0, 0);
		pid_t member = fork();
		if (member == 0) {
			for (;;)
				pause();
		}
		if (write(pipefd[1], &member, sizeof(member)) != sizeof(member))
			_exit(3);
		int st;
		waitpid(member, &st, 0);
		/* The emulator is gone and this is the cleanup after it. */
		_exit(WIFSIGNALED(st) && WTERMSIG(st) == SIGTERM ? 0 : 2);
	}
	setpgid(leader, leader);
	pid_t member;
	CHECK_INT(read(pipefd[0], &member, sizeof(member)), (long)sizeof(member));

	CHECK_INT(quit_signal_group(leader, leader, SIGTERM), 1);
	int st = 0;
	waitpid(leader, &st, 0);
	CHECK(WIFEXITED(st));
	CHECK_INT(WEXITSTATUS(st), 0);           /* the leader ran to its end */
	close(pipefd[0]);
	close(pipefd[1]);
}

int main(void)
{
	test_combo();
	test_stat();
	test_signal_group();
	return check_report("quit");
}
