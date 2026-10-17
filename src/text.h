/* String helpers shared by the launcher and process parsers. */
#ifndef PL_TEXT_H
#define PL_TEXT_H

#include <stddef.h>

/* Copies at most dsz - 1 bytes and NUL-terminates when dsz > 0. */
void str_copy(char *dst, size_t dsz, const char *src);

/* Removes ANSI escapes in place; bluetoothctl emits them even into pipes. */
void strip_ansi(char *s);

/* Decodes the five named XML entities in place, in a single pass. */
void xml_unescape(char *s);

#endif
