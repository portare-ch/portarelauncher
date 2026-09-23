#include "check.h"
#include "proc.h"

#include <time.h>

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

	/* Not there at all: the child's exec fails and it exits 127, the
	 * shell's convention for "command not found". */
	char *const argv[] = { (char *)"/nonexistent/tool", NULL };
	CHECK_INT(proc_run_for(argv, NULL, NULL, 5000), 127);
}

static void test_stdin_is_empty(void)
{
	/* cat reads stdin until EOF. /dev/null ends it at once; an inherited
	 * terminal would hang it until the ceiling. */
	struct lines L;
	long long t0 = now_ms();
	CHECK_INT(sh("cat", &L, 3000), 0);
	CHECK(now_ms() - t0 < 2000);
}

static void test_ceiling(void)
{
	/* The reason proc_run_for exists: bluetoothctl connect to a device
	 * that is not there never returns. */
	struct lines L;
	long long t0 = now_ms();
	CHECK_INT(sh("echo started; sleep 10", &L, 300), PROC_TIMEOUT);
	long long took = now_ms() - t0;
	CHECK(took >= 250);
	CHECK(took < 3000);
	CHECK_INT(L.n, 1);                        /* output before it hung */
	CHECK_STR(L.l[0], "started");
}

int main(void)
{
	test_lines();
	test_long_line();
	test_status();
	test_stdin_is_empty();
	test_ceiling();
	return check_report("proc");
}
