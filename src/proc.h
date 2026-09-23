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

/* Runs argv, feeding each line of its stdout to cb with the newline already
 * stripped. Blank lines are skipped. stderr goes to /dev/null: these tools
 * are chatty about things this UI has no room to show.
 *
 * Returns the exit status, or -1 if it could not be run at all. Blocks for
 * as long as the program takes - a wifi rescan is seconds and a bluetooth
 * scan is however long it was asked to be, so callers say so on screen
 * before calling. */
int proc_run(char *const argv[], void (*cb)(char *line, void *ctx), void *ctx);

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
