#ifndef PL_FAKE_PROC_H
#define PL_FAKE_PROC_H

#define FAKE_MAX_CALLS 32

/* Every command line run since fake_reset, arguments joined by spaces. */
extern char fake_calls[FAKE_MAX_CALLS][256];
extern int fake_n_calls;

void fake_reset(void);
/* What `cmd` (the argv joined by spaces) prints, and its exit status.
 * Anything without a reply fails as if it could not be run. */
void fake_reply(const char *cmd, const char *out, int status);
int  fake_called(const char *cmd);

#endif
