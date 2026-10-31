/* Runs system tools with argv, without a shell, so names and addresses
 * remain literal arguments. Synchronous calls have wall-clock deadlines. */
#ifndef PL_PROC_H
#define PL_PROC_H

#include <stddef.h>

#include "text.h"

#define PROC_FAILED  (-1)
#define PROC_TIMEOUT (-2)
#define PROC_CANCELLED (-3)

/* Default deadline; bluetoothctl can hang even with its own --timeout. */
#define PROC_CEILING_MS 15000

/* Runs argv, passing nonempty stdout lines to cb without newlines.
 * Discards stderr. Blocks up to PROC_CEILING_MS; callers should show busy
 * state first. Returns the exit status, PROC_FAILED or PROC_TIMEOUT. */
int proc_run(char *const argv[], void (*cb)(char *line, void *ctx), void *ctx);

/* proc_run with an explicit wall-clock deadline in milliseconds. */
int proc_run_for(char *const argv[], void (*cb)(char *line, void *ctx),
                 void *ctx, int ceiling_ms);

/* Starts argv without waiting. Double-forks to avoid zombies. */
void proc_spawn(char *const argv[]);

/* One asynchronous child, woken by stdout or Linux pidfd completion.
 * Call init before first use; start never waits for the command. */
struct proc_job {
	int pid, fd, done_fd, status;
	long long deadline;
	char output[64];
	size_t length;
	int overflow;
	int exited;
	void (*line_cb)(char *line, void *ctx);
	void *line_ctx;
	char line[1024];
	size_t line_length;
};
void proc_job_init(struct proc_job *j);
int proc_job_start(struct proc_job *j, char *const argv[], int ceiling_ms);
/* Streams nonempty lines instead of retaining output in the 64-byte buffer. */
int proc_job_start_lines(struct proc_job *j, char *const argv[],
                         void (*cb)(char *line, void *ctx), void *ctx,
                         int ceiling_ms);
/* Returns 1 once collected, 0 while running. Never waits for a live child. */
int proc_job_step(struct proc_job *j);
void proc_job_cancel(struct proc_job *j);
int proc_job_left(const struct proc_job *j);
#endif
