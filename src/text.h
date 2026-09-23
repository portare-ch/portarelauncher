/* Two string helpers that half the program needs.
 *
 * Kept apart from proc.c, which runs other programs, so that code which only
 * needs the strings - and the tests, which swap proc.c for a fake - does not
 * drag fork and exec in with them.
 */
#ifndef PL_TEXT_H
#define PL_TEXT_H

#include <stddef.h>

/* strncpy that always terminates, which strncpy does not. */
void str_copy(char *dst, size_t dsz, const char *src);

/* Removes ANSI escape sequences in place. bluetoothctl colours its output
 * even when it is not talking to a terminal, and the colour lands in the
 * middle of device names. */
void strip_ansi(char *s);

#endif
