/* Copyright (C) 2026-present PortareOS */
#include "check.h"
#include "proc.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include <time.h>
#include <unistd.h>

struct lines {
	char l[8][1100];
	int n;
};

static void collect(char *line, void *ctx)
{
	struct lines *L = ctx;
	if (L->n < 8)
		str_copy(L->l[L->n], sizeof(L->l[0]), line);
	L->n++;
}

static int sh(const char *script, struct lines *L, int ceiling_ms)
{
	char *const argv[] = { (char *)"/bin/sh", (char *)"-c", (char *)script, NULL };
	memset(L, 0, sizeof(*L));
	return proc_run_for(argv, collect, L, ceiling_ms);
}

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void test_lines(void)
{
	struct lines L;
	CHECK_INT(sh("printf 'a\\n\\nb\\n'", &L, 5000), 0);
	CHECK_INT(L.n, 2);                        /* the blank line is skipped */
	CHECK_STR(L.l[0], "a");
	CHECK_STR(L.l[1], "b");

	CHECK_INT(sh("printf 'last'", &L, 5000), 0);   /* no final newline */
	CHECK_INT(L.n, 1);
	CHECK_STR(L.l[0], "last");

	/* stderr is thrown away. */
	CHECK_INT(sh("echo out; echo err >&2", &L, 5000), 0);
	CHECK_INT(L.n, 1);
	CHECK_STR(L.l[0], "out");
}

static void test_long_line(void)
{
	/* Longer than the 1024-byte line buffer: split, nothing lost. */
	struct lines L;
	CHECK_INT(sh("i=0; while [ $i -lt 2000 ]; do printf x; i=$((i+1)); done",
	             &L, 10000), 0);
	CHECK_INT(L.n, 2);
	CHECK_INT(strlen(L.l[0]) + strlen(L.l[1]), 2000);
}

static void test_status(void)
{
	struct lines L;
	CHECK_INT(sh("exit 3", &L, 5000), 3);

	/* A failed exec returns 127, matching the shell convention. */
	char *const argv[] = { (char *)"/nonexistent/tool", NULL };
	CHECK_INT(proc_run_for(argv, NULL, NULL, 5000), 127);
}

static void test_stdin_is_empty(void)
{
	/* /dev/null gives cat immediate EOF instead of waiting for terminal input. */
	struct lines L;
	long long t0 = now_ms();
	CHECK_INT(sh("cat", &L, 3000), 0);
	CHECK(now_ms() - t0 < 2000);
}

static void test_ceiling(void)
{
	/* Bound a hung command, as with an unavailable Bluetooth device. */
	struct lines L;
	long long t0 = now_ms();
	CHECK_INT(sh("echo started; sleep 10", &L, 300), PROC_TIMEOUT);
	long long took = now_ms() - t0;
	CHECK(took >= 250);
	CHECK(took < 3000);
	CHECK_INT(L.n, 1);                        /* output before it hung */
	CHECK_STR(L.l[0], "started");
}

static void test_closed_stdout_ceiling(void)
{
	struct lines L;
	long long start = now_ms();
	CHECK_INT(sh("exec 1>&-; exec sleep 2", &L, 100), PROC_TIMEOUT);
	CHECK(now_ms() - start < 1000);
}

/* Print the group leader PID, then start a delayed writer and hang. */
static int late_writer(int group, const char *mark, struct lines *L)
{
	char script[512];
	snprintf(script, sizeof(script),
	         "echo $$; (sleep 1; echo late > '%s') & sleep 30", mark);
	char *const argv[] = { (char *)"/bin/sh", (char *)"-c", script, NULL };
	memset(L, 0, sizeof(*L));
	return group ? proc_run_group_for(argv, collect, L, 300)
	             : proc_run_for(argv, collect, L, 300);
}

static void test_group_ceiling(void)
{
	char mark[256];
	struct lines L;
	struct timespec wait = { 1, 500000000L };

	/* Killing only the shell leaves its writer running. */
	snprintf(mark, sizeof(mark), "%s/plain", fixture_dir());
	CHECK_INT(late_writer(0, mark, &L), PROC_TIMEOUT);
	nanosleep(&wait, NULL);
	CHECK(access(mark, F_OK) == 0);

	/* Group cleanup stops the writer before returning. */
	snprintf(mark, sizeof(mark), "%s/group", fixture_dir());
	long long start = now_ms();
	CHECK_INT(late_writer(1, mark, &L), PROC_TIMEOUT);
	CHECK(now_ms() - start < 5000);
	CHECK_INT(L.n, 1);
	pid_t group = (pid_t)atoi(L.l[0]);
	CHECK(group > 0);
	CHECK_INT(proc_group_running(group), 0);
	nanosleep(&wait, NULL);
	CHECK(access(mark, F_OK) != 0);

#ifdef __linux__
	/* Unreaped zombies must not block group cleanup. */
	snprintf(mark, sizeof(mark), "%s/zombie", fixture_dir());
	pid_t keeper = fork();
	if (keeper == 0) {
		prctl(PR_SET_CHILD_SUBREAPER, 1);
		pid_t runner = fork();
		if (runner == 0) {
			struct lines LL;
			_exit(late_writer(1, mark, &LL) == PROC_TIMEOUT ? 0 : 1);
		}
		alarm(10);                    /* a hang fails the check below */
		int st = 0;
		while (waitpid(runner, &st, 0) < 0 && errno == EINTR)
			;
		_exit(WIFEXITED(st) ? WEXITSTATUS(st) : 2);
	}
	int kst = 0;
	waitpid(keeper, &kst, 0);
	CHECK(WIFEXITED(kst) && WEXITSTATUS(kst) == 0);
	CHECK(access(mark, F_OK) != 0);
#endif

	/* Stop workers when the helper dies before its deadline. */
	snprintf(mark, sizeof(mark), "%s/killed", fixture_dir());
	char script[512];
	snprintf(script, sizeof(script),
	         "echo $$; (sleep 1; echo late > '%s') >/dev/null 2>&1 & kill -9 $$", mark);
	char *const killed[] = { (char *)"/bin/sh", (char *)"-c", script, NULL };
	memset(&L, 0, sizeof(L));
	start = now_ms();
	CHECK_INT(proc_run_group_for(killed, collect, &L, 30000), PROC_FAILED);
	CHECK(now_ms() - start < 5000);
	group = L.n == 1 ? (pid_t)atoi(L.l[0]) : 0;
	CHECK(group > 0);
	CHECK_INT(proc_group_running(group), 0);
	nanosleep(&wait, NULL);
	CHECK(access(mark, F_OK) != 0);

	/* A worker holding stdout must not delay cleanup or lose the child's output. */
	snprintf(script, sizeof(script),
	         "echo $$; echo error=busy; (sleep 1; echo late; echo late > '%s') & kill -9 $$",
	         mark);
	char *const holder[] = { (char *)"/bin/sh", (char *)"-c", script, NULL };
	memset(&L, 0, sizeof(L));
	start = now_ms();
	CHECK_INT(proc_run_group_for(holder, collect, &L, 30000), PROC_FAILED);
	CHECK(now_ms() - start < 900);
	CHECK_INT(L.n, 2);
	CHECK_STR(L.l[1], "error=busy");
	group = (pid_t)atoi(L.l[0]);
	CHECK(group > 0);
	CHECK_INT(proc_group_running(group), 0);
	nanosleep(&wait, NULL);
	CHECK(access(mark, F_OK) != 0);

	/* Preserve normal output and exit status. */
	char *const ok[] = { (char *)"/bin/sh", (char *)"-c", (char *)"echo hi; exit 3", NULL };
	memset(&L, 0, sizeof(L));
	CHECK_INT(proc_run_group_for(ok, collect, &L, 5000), 3);
	CHECK_STR(L.l[0], "hi");
}

int main(void)
{
	test_closed_stdout_ceiling();
	test_group_ceiling();
	test_lines();
	test_long_line();
	test_status();
	test_stdin_is_empty();
	test_ceiling();
	return check_report("proc");
}
