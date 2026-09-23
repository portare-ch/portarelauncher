#include "proc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int proc_run(char *const argv[], void (*cb)(char *line, void *ctx), void *ctx)
{
	int fd[2];
	if (pipe(fd) < 0)
		return -1;

	pid_t pid = fork();
	if (pid < 0) {
		close(fd[0]);
		close(fd[1]);
		return -1;
	}
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], STDOUT_FILENO);
		int null = open("/dev/null", O_WRONLY);
		if (null >= 0) {
			dup2(null, STDERR_FILENO);
			close(null);
		}
		close(fd[1]);
		execvp(argv[0], argv);
		_exit(127);
	}

	close(fd[1]);
	FILE *f = fdopen(fd[0], "r");
	if (f) {
		char line[512];
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\n")] = '\0';
			if (cb && line[0])
				cb(line, ctx);
		}
		fclose(f);
	} else {
		close(fd[0]);
	}

	int status = 0;
	while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
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

void str_copy(char *dst, size_t dsz, const char *src)
{
	size_t n = strlen(src);
	if (dsz == 0)
		return;
	if (n >= dsz)
		n = dsz - 1;
	memmove(dst, src, n);
	dst[n] = '\0';
}

void strip_ansi(char *s)
{
	char *w = s;
	for (const char *r = s; *r; r++) {
		if (*r != '\033') {
			*w++ = *r;
			continue;
		}
		/* CSI: ESC [ ... final byte in 0x40-0x7e. Anything else after
		 * ESC is a two-byte sequence, so drop one more and carry on. */
		if (r[1] == '[') {
			r += 2;
			while (*r && (*r < 0x40 || *r > 0x7e))
				r++;
			if (!*r)
				break;
		} else if (r[1]) {
			r++;
		} else {
			break;
		}
	}
	*w = '\0';
}
