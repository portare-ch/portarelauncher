#include "check.h"
#ifdef __linux__
#include "input.h"
#include "proc.h"
#include "timeutil.h"
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>

static void input_during_job(void)
{
	int p[2]; CHECK_INT(pipe(p),0);
	fcntl(p[0],F_SETFL,O_NONBLOCK);
	struct input in={.n=1,.aux_fd=-1,.display_fd=-1,.notify_fd=-1}; in.fd[0]=p[0];
	struct proc_job j; proc_job_init(&j);
	char *args[]={"/bin/sh","-c","exec sleep 10",NULL};
	CHECK_INT(proc_job_start(&j,args,5000),0);
	struct input_event ev={.type=EV_KEY,.code=KEY_DOWN,.value=1};
	CHECK_INT(write(p[1],&ev,sizeof(ev)),sizeof(ev));
	long long start=now_ms();
	CHECK_INT(input_wait_job(&in,5000,j.fd,j.done_fd),ACT_DOWN);
	CHECK(now_ms()-start<500); CHECK(j.pid>0);
	proc_job_cancel(&j);
	close(p[0]); close(p[1]);
}
static void completion_wakes(void)
{
	struct input in={.aux_fd=-1,.display_fd=-1,.notify_fd=-1};
	struct proc_job j; proc_job_init(&j);
	char *args[]={"/bin/sh","-c","printf 'SLUS-20946\\n'",NULL};
	CHECK_INT(proc_job_start(&j,args,5000),0);
	CHECK_INT(input_wait_job(&in,5000,j.fd,j.done_fd),ACT_METADATA);
	while (!proc_job_step(&j))
		CHECK_INT(input_wait_job(&in,5000,j.fd,j.done_fd),ACT_METADATA);
	CHECK_STR(j.output,"SLUS-20946\n"); CHECK_INT(j.status,0);
	char *silent[]={"/bin/sh","-c","exec 1>&-; sleep 0.05; exit 1",NULL};
	CHECK_INT(proc_job_start(&j,silent,5000),0);
	CHECK_INT(input_wait_job(&in,5000,j.fd,j.done_fd),ACT_METADATA);
	CHECK(!proc_job_step(&j)); CHECK_INT(j.fd,-1);
	CHECK_INT(input_wait_job(&in,5000,j.fd,j.done_fd),ACT_METADATA);
	CHECK(proc_job_step(&j)); CHECK_INT(j.status,1);
}
static void back_beats_ready_output(void)
{
	int keys[2], output[2];
	CHECK_INT(pipe(keys), 0); CHECK_INT(pipe(output), 0);
	fcntl(keys[0], F_SETFL, O_NONBLOCK);
	struct input in = { .n=1, .aux_fd=-1, .display_fd=-1, .notify_fd=-1 };
	in.fd[0] = keys[0];
	struct input_event ev = { .type=EV_KEY, .code=KEY_BACKSPACE, .value=1 };
	CHECK_INT(write(keys[1], &ev, sizeof(ev)), sizeof(ev));
	CHECK_INT(write(output[1], "ready", 5), 5);
	CHECK_INT(input_wait_job(&in, 500, output[0], -1), ACT_BACK);
	CHECK_INT(input_wait_job(&in, 500, output[0], -1), ACT_METADATA);
	close(keys[0]); close(keys[1]); close(output[0]); close(output[1]);
}

static void cancel_descendants(void)
{
	char dir[64], gate[96], leaked[96];
	snprintf(dir, sizeof(dir), "%s", fixture_dir());
	snprintf(gate, sizeof(gate), "%s/gate", dir);
	snprintf(leaked, sizeof(leaked), "%s/leaked", dir);
	char *args[] = { "/bin/sh", "-c",
		"(while [ ! -e \"$1\" ]; do sleep 0.01; done; echo leaked > \"$2\") & echo ready; wait",
		"sh", gate, leaked, NULL };
	struct proc_job j;
	proc_job_init(&j);
	struct input in = { .aux_fd=-1, .display_fd=-1, .notify_fd=-1 };
	CHECK_INT(proc_job_start(&j, args, 5000), 0);
	while (!j.length && !proc_job_step(&j))
		input_wait_job(&in, proc_job_left(&j), j.fd, j.done_fd);
	CHECK_STR(j.output, "ready\n");
	proc_job_cancel(&j);
	CHECK_INT(j.pid, 0); CHECK_INT(j.fd, -1); CHECK_INT(j.done_fd, -1);
	fixture_write(gate, "go\n");
	poll(NULL, 0, 250);
	CHECK(access(leaked, F_OK) != 0);
	fixture_cleanup(dir);
}

struct received { int n; size_t bytes; char first[32], last[32]; };
static void line_received(char *line, void *ctx)
{
	struct received *r = ctx;
	if (!r->n) snprintf(r->first, sizeof(r->first), "%s", line);
	snprintf(r->last, sizeof(r->last), "%s", line);
	r->bytes += strlen(line);
	r->n++;
}
static void streamed_output(void)
{
	struct proc_job j;
	proc_job_init(&j);
	struct received r = {0};
	struct input in = { .aux_fd=-1, .display_fd=-1, .notify_fd=-1 };
	char *args[] = { "/bin/sh", "-c",
		"printf 'first\\n\\n'; i=0; while [ $i -lt 2000 ]; do printf 'Device AA:BB:CC:DD:EE:FF Headset\\n'; i=$((i+1)); done; printf last", NULL };
	CHECK_INT(proc_job_start_lines(&j, args, line_received, &r, 5000), 0);
	while (!proc_job_step(&j))
		input_wait_job(&in, proc_job_left(&j), j.fd, j.done_fd);
	CHECK_INT(j.status, 0);
	CHECK_INT(r.n, 2002);
	CHECK_STR(r.first, "first"); CHECK_STR(r.last, "last");
	CHECK_INT(r.bytes, 2000 * strlen("Device AA:BB:CC:DD:EE:FF Headset") + 9);
	CHECK_INT(j.fd, -1); CHECK_INT(j.done_fd, -1);

	char *silent[] = { "/bin/sh", "-c", "exec 1>&-; exec sleep 10", NULL };
	CHECK_INT(proc_job_start_lines(&j, silent, line_received, &r, 100), 0);
	long long start = now_ms();
	while (!proc_job_step(&j))
		input_wait_job(&in, proc_job_left(&j), j.fd, j.done_fd);
	CHECK_INT(j.status, PROC_TIMEOUT);
	CHECK(now_ms() - start < 1000);
}
#endif
int main(void)
{
#ifdef __linux__
	input_during_job(); completion_wakes(); back_beats_ready_output(); streamed_output(); cancel_descendants();
#endif
	return check_report("metadata input");
}
