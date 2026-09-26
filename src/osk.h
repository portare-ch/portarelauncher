/* An on-screen keyboard, for the one thing on this device that needs typing:
 * the password of a Wi-Fi network that has never been joined.
 *
 * Every printable ASCII character is reachable, because a WPA passphrase may
 * use any of them. Two layers of four ten-key rows, the digits on top of
 * both, and a bottom row of wide keys - SHIFT, the layer switch, SPACE, DEL,
 * DONE. See docs/mockup.txt for the layout at the real grid.
 *
 * This file knows nothing about Wi-Fi. It edits a string and says when the
 * user is done with it or wants out; what the string is for is the caller's
 * business.
 */
#ifndef PL_OSK_H
#define PL_OSK_H

#include "input.h"
#include "term.h"

#define OSK_MAX 63           /* a WPA passphrase's ceiling, and ours */

enum osk_result {
	OSK_NONE = 0,
	OSK_DONE,            /* the text is complete and long enough      */
	OSK_CANCEL,          /* back on an empty field: leave             */
};

struct osk {
	char text[OSK_MAX + 1];
	int  len;
	int  min_len;        /* DONE is refused below this                */
	int  layer;          /* 0 letters, 1 symbols                      */
	int  shift;          /* 0 off, 1 the next letter, 2 locked        */
	int  row, col;       /* 0-3 the character rows, 4 the bottom row  */
	int  hidden;         /* draw bullets instead of the text          */
};

void osk_init(struct osk *k, int min_len);

/* Feeds one action to the keyboard. Directions move, confirm presses the
 * focused key, and the other buttons are shortcuts that work from
 * anywhere: back deletes (and leaves, on an empty field), the settings
 * button types a space, L1 is shift, START is done and SELECT hides or
 * shows. */
enum osk_result osk_action(struct osk *k, enum action a);

/* Draws the field at row `y` and the keys below it. Returns the first row
 * below what it drew, so the caller can put a line of its own there. */
unsigned osk_draw(const struct osk *k, struct term *t, unsigned y);

#endif
