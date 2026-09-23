/* Running another program and reading what it says.
 *
 * This program is a front-end to tools the system already has: nmcli,
 * usbgadget, bluetoothctl, runemu.sh. None of them is reimplemented here and
 * none of them is called through a shell.
 *
 * No shell, ever. The arguments are network names, device names and MAC
 * addresses, and those come from the air and from other people. A network
 * called `; rm -rf /` has to stay a string.
 */
#ifndef PL_PROC_H
#define PL_PROC_H

#include <stddef.h>

/* Could not be run at all, and ran past its ceiling. Both are failures; a
 * caller that wants to tell the user which can. */
#define PROC_FAILED  (-1)
#define PROC_TIMEOUT (-2)

/* Nothing this program shells out to may block it forever. Measured on the
 * device: `bluetoothctl connect` against a device that is not there never
 * returns at all, and this process owns the panel, so "never" means a black
 * screen and a battery pull. Hence a wall-clock ceiling on every call. */
#define PROC_CEILING_MS 15000

/* Runs argv, feeding each line of its stdout to cb with the newline already
 * stripped. Blank lines are skipped. stderr goes to /dev/null: these tools
 * are chatty about things this UI has no room to show.
 *
 * Returns the exit status, PROC_FAILED, or PROC_TIMEOUT. Blocks for as long
 * as the program takes, up to the ceiling - a wifi rescan is seconds and a
 * bluetooth scan is however long it was asked to be, so callers say so on
 * screen before calling. */
int proc_run(char *const argv[], void (*cb)(char *line, void *ctx), void *ctx);

/* The same, with the ceiling given: short for something that answers from a
 * cache, long for something that has to talk to a radio. */
int proc_run_for(char *const argv[], void (*cb)(char *line, void *ctx),
                 void *ctx, int ceiling_ms);

/* Starts argv and does not wait for it. Double-forks so the child is
 * reparented and there is no zombie to reap, because the point of calling
 * this is that nothing here wants to know how it went. */
void proc_spawn(char *const argv[]);

/* strncpy that always terminates, which strncpy does not. */
void str_copy(char *dst, size_t dsz, const char *src);

/* Removes ANSI escape sequences in place. bluetoothctl colours its output
 * even when it is not talking to a terminal, and the colour lands in the
 * middle of device names. */
void strip_ansi(char *s);

#endif
