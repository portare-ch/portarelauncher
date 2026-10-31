#include "proc.h"
#include "timeutil.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

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

	/* Use read/poll so the deadline also applies while waiting for stdout. */
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
	for (;;) {
		pid_t r = waitpid(pid, &status, timed_out ? 0 : WNOHANG);
		if (r == pid) break;
		if (r < 0 && errno != EINTR) return PROC_FAILED;
		if (timed_out) continue;
		long long left = deadline - now_ms();
		if (left <= 0) {
			timed_out = 1;
			kill(pid, SIGKILL);
		} else {
			/* EOF can precede process exit; it must not bypass the deadline. */
			poll(NULL, 0, left < 10 ? (int)left : 10);
		}
	}
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
		/* The grandchild is reparented when this intermediate process exits. */
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

#ifdef __linux__
#include <sys/syscall.h>
#endif
#include <limits.h>

void proc_job_init(struct proc_job *j)
{
	memset(j, 0, sizeof(*j));
	j->fd = j->done_fd = -1;
	j->status = PROC_FAILED;
}
static void job_close(struct proc_job *j)
{
	if (j->fd >= 0) close(j->fd);
	if (j->done_fd >= 0) close(j->done_fd);
	j->fd = j->done_fd = -1;
	j->pid = 0;
}
void proc_job_cancel(struct proc_job *j)
{
	if (j->pid > 0) kill(-j->pid, SIGKILL);
	if (j->pid > 0 && !j->exited) {
		kill(j->pid, SIGKILL);
		while (waitpid(j->pid, NULL, 0) < 0 && errno == EINTR) {}
	}
	job_close(j);
	j->status = PROC_FAILED;
}
int proc_job_start(struct proc_job *j, char *const argv[], int ceiling_ms)
{
	if (j->pid || ceiling_ms <= 0) return -1;
	proc_job_init(j);
	int p[2];
	if (pipe(p)) return -1;
	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	pid_t pid = fork();
	if (!pid) {
		setpgid(0, 0);
		close(p[0]);
		dup2(p[1], STDOUT_FILENO);
		close(p[1]);
		int null = open("/dev/null", O_RDWR);
		if (null >= 0) {
			dup2(null, STDIN_FILENO); dup2(null, STDERR_FILENO);
			if (null > 2) close(null);
		}
		execvp(argv[0], argv);
		_exit(127);
	}
	close(p[1]);
	if (pid < 0) { close(p[0]); return -1; }
	setpgid(pid, pid);
	j->pid = pid; j->fd = p[0];
	fcntl(j->fd, F_SETFL, O_NONBLOCK);
#if defined(__linux__) && defined(SYS_pidfd_open)
	j->done_fd = (int)syscall(SYS_pidfd_open, pid, 0);
#endif
	/* Without completion notification, keep filenames rather than poll a timer. */
	if (j->done_fd < 0) { proc_job_cancel(j); return -1; }
	fcntl(j->done_fd, F_SETFD, FD_CLOEXEC);
	j->deadline = now_ms() + ceiling_ms;
	return 0;
}
int proc_job_start_lines(struct proc_job *j, char *const argv[],
                         void (*cb)(char *line, void *ctx), void *ctx,
                         int ceiling_ms)
{
	int r = proc_job_start(j, argv, ceiling_ms);
	if (!r) { j->line_cb = cb; j->line_ctx = ctx; }
	return r;
}
static void job_line(struct proc_job *j)
{
	if (j->line_length && j->line_cb) {
		j->line[j->line_length] = 0;
		j->line_cb(j->line, j->line_ctx);
	}
	j->line_length = 0;
}
static void job_read(struct proc_job *j)
{
	char b[512];
	/* Bounded work even if an accidentally substituted helper floods stdout. */
	for (int i = 0; i < 8 && j->fd >= 0; i++) {
		ssize_t n = read(j->fd, b, sizeof(b));
		if (n <= 0) {
			if (!n || (errno != EAGAIN && errno != EINTR)) {
				job_line(j);
				close(j->fd); j->fd = -1;
			}
			break;
		}
		for (ssize_t k = 0; k < n; k++) {
			if (j->line_cb) {
				if (b[k] == '\n' || j->line_length + 1 >= sizeof(j->line)) {
					job_line(j);
					if (b[k] != '\n') j->line[j->line_length++] = b[k];
				} else j->line[j->line_length++] = b[k];
			} else if (j->length < sizeof(j->output)-1) j->output[j->length++] = b[k];
			else j->overflow = 1;
		}
	}
	j->output[j->length] = 0;
}
int proc_job_step(struct proc_job *j)
{
	if (!j->pid) return 0;
	job_read(j);
	if (!j->exited) {
		int st;
		pid_t r = waitpid(j->pid, &st, WNOHANG);
		if (r == j->pid || (r < 0 && errno != EINTR)) {
			j->exited = 1;
			j->status = r > 0 && WIFEXITED(st) ? WEXITSTATUS(st) : PROC_FAILED;
			if (j->done_fd >= 0) close(j->done_fd);
			j->done_fd = -1;
		}
	}
	if (j->exited && j->fd < 0) {
		job_close(j);
		return 1;
	}
	if (now_ms() >= j->deadline) {
		proc_job_cancel(j); j->status = PROC_TIMEOUT; return 1;
	}
	return 0;
}
int proc_job_left(const struct proc_job *j)
{
	if (!j->pid) return -1;
	long long left = j->deadline - now_ms();
	return left <= 0 ? 0 : left > INT_MAX ? INT_MAX : (int)left;
}
