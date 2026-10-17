/* Runs system tools with argv, without a shell, so names and addresses
 * remain literal arguments. Synchronous calls have wall-clock deadlines. */
#ifndef PL_PROC_H
#define PL_PROC_H

#include <stddef.h>

#include "text.h"

#define PROC_FAILED  (-1)
#define PROC_TIMEOUT (-2)

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

#endif
