/* What PortScope shows, kept apart from where it comes from.
 *
 * Events go in as the kernel delivers them, type, code and value; what
 * comes out is which control is held, where each axis is, the last few
 * events and how often the pad reports. No device, no screen: the tests
 * feed this directly.
 *
 * Two layers, one at a time. The virtual pad is what InputPlumber presents
 * and what games read; the raw layer is the MCU's own device and the
 * gpio-keys that carry the paddles, as they are before InputPlumber maps
 * them. They name some controls differently, so each has its own map.
 */
#ifndef PL_SCOPE_H
#define PL_SCOPE_H

#include <stddef.h>

/* linux/input.h's values, spelled out as quit.h does so this builds and is
 * tested on a machine without that header. portscope.c checks them. */
#define SC_EV_SYN      0x00
#define SC_EV_KEY      0x01
#define SC_EV_ABS      0x03
#define SC_SYN_REPORT  0

#define SC_KEY_ESC     1
#define SC_BTN_BACK    0x116
#define SC_BTN_SOUTH   0x130
#define SC_BTN_EAST    0x131
#define SC_BTN_C       0x132
#define SC_BTN_NORTH   0x133
#define SC_BTN_WEST    0x134
#define SC_BTN_Z       0x135
#define SC_BTN_TL      0x136
#define SC_BTN_TR      0x137
#define SC_BTN_TL2     0x138
#define SC_BTN_TR2     0x139
#define SC_BTN_SELECT  0x13a
#define SC_BTN_START   0x13b
#define SC_BTN_MODE    0x13c
#define SC_BTN_THUMBL  0x13d
#define SC_BTN_THUMBR  0x13e
#define SC_BTN_DPAD_UP    0x220
#define SC_BTN_DPAD_DOWN  0x221
#define SC_BTN_DPAD_LEFT  0x222
#define SC_BTN_DPAD_RIGHT 0x223
#define SC_BTN_TRIGGER_HAPPY3 0x2c2
#define SC_BTN_TRIGGER_HAPPY4 0x2c3

#define SC_ABS_X       0x00
#define SC_ABS_Y       0x01
#define SC_ABS_Z       0x02
#define SC_ABS_RX      0x03
#define SC_ABS_RY      0x04
#define SC_ABS_RZ      0x05
#define SC_ABS_HAT0X   0x10
#define SC_ABS_HAT0Y   0x11

enum scope_layer { SCOPE_VIRTUAL = 0, SCOPE_RAW };

/* By where the control sits on the Nova, not by what any layer calls it. */
enum scope_ctl {
	CTL_L1, CTL_R1, CTL_L2, CTL_R2, CTL_L3, CTL_R3,
	CTL_UP, CTL_DOWN, CTL_LEFT, CTL_RIGHT,
	CTL_TOP, CTL_WEST, CTL_EAST, CTL_BOTTOM,    /* the four face buttons */
	CTL_SELECT, CTL_HOME, CTL_START, CTL_M1, CTL_M2,
	N_CTL
};

enum scope_axis { AX_LX, AX_LY, AX_RX, AX_RY, AX_LT, AX_RT, N_AX };

#define SCOPE_LAST   2      /* the rows under "Last input"              */
#define SCOPE_RATE_N 1024   /* report times kept; 200 Hz needs 200      */

struct scope_event {
	unsigned type, code;
	int value;
};

struct scope {
	enum scope_layer layer;
	unsigned char held[N_CTL];
	int value[N_AX];
	int min[N_AX], max[N_AX];
	struct scope_event last[SCOPE_LAST];
	int nlast;
	long long rate_t[SCOPE_RATE_N];   /* ms, oldest overwritten first */
	int rate_head, rate_n;
};

/* Forgets everything and takes the map of the given layer. The ranges
 * start as the virtual pad's, 0..255; scope_range sets the real ones. */
void scope_reset(struct scope *s, enum scope_layer layer);

/* The range the device reports for an ABS code and where the axis is
 * now, from EVIOCGABS. */
void scope_range(struct scope *s, unsigned abs_code, int min, int max, int value);

/* One event, at t_ms. Returns 1 when something on screen changes. */
int scope_feed(struct scope *s, unsigned type, unsigned code, int value,
               long long t_ms);

/* Sticks from -1 to 1, centre 0; triggers from 0 to 1. */
double scope_axis(const struct scope *s, enum scope_axis a);

/* A control is lit: a button held, a trigger past zero. */
int scope_lit(const struct scope *s, enum scope_ctl c);

/* Reports in the second up to now_ms: the pad's rate while it moves, 0
 * once it has been still for a second. */
int scope_rate(const struct scope *s, long long now_ms);

/* The kernel's name for a code, "ABS_RY", "BTN_SOUTH"; one it does not
 * know is printed as its number. */
const char *scope_code_name(unsigned type, unsigned code, char *buf, size_t n);

#endif
