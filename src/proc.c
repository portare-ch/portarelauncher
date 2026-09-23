#include "proc.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int proc_run(char *const argv[], void (*cb)(char *line, void *ctx), void *ctx)
{
	return proc_run_for(argv, cb, ctx, PROC_CEILING_MS);
}

int proc_run_for(char *const argv[], void (*cb)(char *line, void *ctx),
                 void *ctx, int ceiling_ms)
{
	int fd[2];
	if (pipe(fd) < 0)
		return PROC_FAILED;

	pid_t pid = fork();
	if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		return PROC_FAILED;
	}
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO);
		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO);
			dup2(null, STDERR_FILENO);
			if (null > STDERR_FILENO)
				close(null);
		}
		close(fd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fd[1]);

	/* Read by hand rather than through stdio, because stdio has no way to
	 * say "and give up at some point". */
	char line[1024];
	size_t len = 0;
	long long deadline = now_ms() + ceiling_ms;
	int timed_out = 0;

	for (;;) {
		long long left = deadline - now_ms();
		if (left <= 0) {
			timed_out = 1;
			break;
		}

		struct pollfd p = { .fd = fd[0], .events = POLLIN };
		int r = poll(&p, 1, (int)left);
		if (r < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (r == 0) {
			timed_out = 1;
			break;
		}

		char buf[512];
		ssize_t n = read(fd[0], buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			break;                     /* the child closed stdout */

		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] == '\n' || len + 1 >= sizeof(line)) {
				line[len] = '\0';
				if (cb && line[0])
					cb(line, ctx);
				len = 0;
				if (buf[i] != '\n')
					line[len++] = buf[i];
			} else {
				line[len++] = buf[i];
			}
		}
	}

	if (len && cb) {
		line[len] = '\0';
		if (line[0])
			cb(line, ctx);
	}

	if (timed_out)
		kill(pid, SIGKILL);

	close(fd[0]);

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	if (timed_out)
		return PROC_TIMEOUT;
	return WIFEXITED(status) ? WEXITSTATUS(status) : PROC_FAILED;
}

void proc_spawn(char *const argv[])
{
	pid_t pid = fork();
	if (pid < 0)
		return;

	if (pid == 0) {
		/* The middle process exits immediately, so the grandchild is
		 * reparented to init and nobody here has to reap it. */
		if (fork() == 0) {
			int null = open("/dev/null", O_RDWR);
			if (null >= 0) {
				dup2(null, STDIN_FILENO);
				dup2(null, STDOUT_FILENO);
				dup2(null, STDERR_FILENO);
				if (null > STDERR_FILENO)
					close(null);
			}
			setsid();
			execvp(argv[0], argv);
		}
		_exit(0);
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
}
