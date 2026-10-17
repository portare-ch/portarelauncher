#ifndef PL_FAKE_PROC_H
#define PL_FAKE_PROC_H

#define FAKE_MAX_CALLS 32

/* Every command line run since fake_reset, arguments joined by spaces. */
extern char fake_calls[FAKE_MAX_CALLS][256];
extern int fake_n_calls;

void fake_reset(void);
/* Configures output and exit status for argv joined with spaces.
 * Commands without a configured reply fail. */
void fake_reply(const char *cmd, const char *out, int status);
int  fake_called(const char *cmd);

#endif
