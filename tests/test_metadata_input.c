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
	struct input in={.n=1,.aux_fd=-1,.notify_fd=-1}; in.fd[0]=p[0];
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
	struct input in={.aux_fd=-1,.notify_fd=-1};
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
#endif
int main(void)
{
#ifdef __linux__
	input_during_job(); completion_wakes();
#endif
	return check_report("metadata input");
}
