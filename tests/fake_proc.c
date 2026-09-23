/* proc.c without the processes: replays recorded output instead of running
 * nmcli or bluetoothctl, and remembers what it was asked to run.
 *
 * A test says what a command line prints and how it exits, runs the code
 * under test, then looks at the result and at the calls that were made.
 */
#include "fake_proc.h"
#include "proc.h"

#include <stdio.h>
#include <string.h>

#define MAX_REPLIES 32

static struct {
	char cmd[256];
	const char *out;
	int status;
} replies[MAX_REPLIES];
static int n_replies;

char fake_calls[FAKE_MAX_CALLS][256];
int fake_n_calls;

void fake_reset(void)
{
	n_replies = 0;
	fake_n_calls = 0;
}

void fake_reply(const char *cmd, const char *out, int status)
{
	if (n_replies >= MAX_REPLIES)
		return;
	snprintf(replies[n_replies].cmd, sizeof(replies[0].cmd), "%s", cmd);
	replies[n_replies].out = out;
	replies[n_replies].status = status;
	n_replies++;
}

int fake_called(const char *cmd)
{
	for (int i = 0; i < fake_n_calls; i++)
		if (strcmp(fake_calls[i], cmd) == 0)
			return 1;
	return 0;
}

static void join(char *const argv[], char *out, size_t osz)
{
	out[0] = '\0';
	for (int i = 0; argv[i]; i++) {
		if (i)
			strncat(out, " ", osz - strlen(out) - 1);
		strncat(out, argv[i], osz - strlen(out) - 1);
	}
}

int proc_run_for(char *const argv[], void (*cb)(char *line, void *ctx),
                 void *ctx, int ceiling_ms)
{
	(void)ceiling_ms;
	char cmd[256];
	join(argv, cmd, sizeof(cmd));
	if (fake_n_calls < FAKE_MAX_CALLS)
		snprintf(fake_calls[fake_n_calls++], sizeof(fake_calls[0]), "%s", cmd);

	for (int i = 0; i < n_replies; i++) {
		if (strcmp(replies[i].cmd, cmd) != 0)
			continue;
		/* Line by line, blank lines skipped, the way proc.c delivers. */
		const char *p = replies[i].out ? replies[i].out : "";
		while (*p) {
			char line[1024];
			size_t n = strcspn(p, "\n");
			if (n >= sizeof(line))
				n = sizeof(line) - 1;
			memcpy(line, p, n);
			line[n] = '\0';
			if (cb && line[0])
				cb(line, ctx);
			p += strcspn(p, "\n");
			if (*p == '\n')
				p++;
		}
		return replies[i].status;
	}
	return PROC_FAILED;
}

int proc_run(char *const argv[], void (*cb)(char *line, void *ctx), void *ctx)
{
	return proc_run_for(argv, cb, ctx, PROC_CEILING_MS);
}

void proc_spawn(char *const argv[])
{
	proc_run_for(argv, NULL, NULL, 0);
}
