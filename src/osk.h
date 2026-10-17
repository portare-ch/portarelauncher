/* On-screen keyboard for printable ASCII, used for Wi-Fi passwords.
 * Two character layers share digits and a row of wide action keys.
 * See docs/mockup.txt for the layout. */
#ifndef PL_OSK_H
#define PL_OSK_H

#include "input.h"
#include "term.h"

#define OSK_MAX 63           /* a WPA passphrase's ceiling, and ours */

enum osk_result {
	OSK_NONE = 0,
	OSK_DONE,            /* the text is complete and long enough */
	OSK_CANCEL,          /* back on an empty field: leave */
};

struct osk {
	char text[OSK_MAX + 1];
	int  len;
	int  min_len;        /* DONE is refused below this */
	int  layer;          /* 0 letters, 1 symbols */
	int  shift;          /* 0 off, 1 the next letter, 2 locked */
	int  row, col;       /* 0-3 the character rows, 4 the bottom row */
	int  hidden;         /* draw bullets instead of the text */
};

void osk_init(struct osk *k, int min_len);

/* Directions move; confirm presses the focused key. Back deletes or cancels
 * an empty field, MENU types space, L1 shifts, START finishes, SELECT toggles
 * password visibility. */
enum osk_result osk_action(struct osk *k, enum action a);

/* Draws the field at y and keys below; returns the first unused row. */
unsigned osk_draw(const struct osk *k, struct term *t, unsigned y);

#endif
