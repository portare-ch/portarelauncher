/* Copyright (C) 2026-present PortareOS */
/* Gamepad and keyboard input from evdev. Polls all key-capable devices
 * and blocks while idle; held directions use a repeat timeout. */
#ifndef PL_INPUT_H
#define PL_INPUT_H

enum action {
	ACT_NONE = 0,
	ACT_UP,
	ACT_DOWN,
	ACT_LEFT,
	ACT_RIGHT,
	ACT_CONFIRM,
	ACT_BACK,
	ACT_MENU,     /* the settings key */
	ACT_ALT,      /* L1: shift on the keyboard, nothing elsewhere */
	ACT_HOME,     /* Home: the consoles list, from anywhere */
	ACT_START,    /* START: "open settings" everywhere except the keyboard */
	ACT_SELECT,   /* SELECT: only the keyboard uses it, to hide the text */
	ACT_FAV,      /* the left face button: favourite on and off, in any list of games */
	ACT_TICK,     /* the idle timeout expired; nothing was pressed */
	ACT_AUX,      /* the auxiliary fd has something to read */
	ACT_DISPLAY,  /* the display fd has something to read: a hotplug */
	ACT_METADATA, /* metadata stdout or child completion; no user activity */
	ACT_QUIT,     /* only bound to a keyboard escape hatch */
};

#define INPUT_MAX_DEV 24

struct input {
	int fd[INPUT_MAX_DEV];
	int n;
	/* The left stick's range, per device and axis (X, Y), read when the
	 * device is opened. Left at 0..0 when the device reports none. */
	int abs_min[INPUT_MAX_DEV][2];
	int abs_max[INPUT_MAX_DEV][2];
	/* Status notification pipe, polled separately from evdev events. */
	int aux_fd;
	/* Display hotplug uevents. */
	int display_fd;
	/* Watches /dev/input for hotplug and InputPlumber device recreation. */
	int notify_fd;
	int quit_only;         /* the mask input_quit_only set, reapplied */
	enum action held;      /* direction currently held, for repeat */
	int repeats;           /* how many repeats have fired */
};

/* Clears *in before opening anything, so input_set_aux and
 * input_set_display come after it. */
int  input_open(struct input *in);

/* Adds devices and the hotplug watch to pfd (INPUT_MAX_DEV + 1 slots),
 * returning the count. input_refresh returns 1 after reopening devices;
 * skip reads from that poll round because its descriptors have been closed. */
struct pollfd;
int  input_fill_poll(struct input *in, struct pollfd *pfd);
int  input_refresh(struct input *in, const struct pollfd *pfd, int n);
void input_set_aux(struct input *in, int fd);
void input_set_display(struct input *in, int fd);
void input_close(struct input *in);

/* Retroid: right confirms, bottom goes back. Shapes: bottom confirms,
 * right goes back. Top opens settings in both. Applies on the next press. */
void input_set_layout(int retroid);

/* Discards events queued while a child owned the panel, including its
 * quit combo, so they do not trigger menu actions on return. */
void input_drain(struct input *in);

/* While a child owns the panel, on=1 masks input to Home and START for
 * the quit combo; on=0 restores all events. Without EVIOCSMASK, all events
 * are delivered and filtered by the caller. */
void input_quit_only(struct input *in, int on);

/* Reads whatever is waiting and returns 1 if it completed Home + START. */
struct quit_combo;
int  input_quit_read(struct input *in, struct quit_combo *q);

/* Which way a stick axis points: -1, 0 inside the dead zone, or 1. The
 * dead zone is the middle half of the device's range (min..max), so a pad
 * that reports 0..255 moves the menu like one that reports -32768..32767.
 * A device without a range (max <= min) counts as the latter. */
int  input_stick_dir(int value, int min, int max);

/* Returns one action. idle_ms bounds the wait; negative waits indefinitely.
 * Held-direction repeat may shorten it. ACT_TICK marks a timeout; ACT_NONE
 * marks an unbound event. */
enum action input_wait(struct input *in, int idle_ms);
enum action input_wait_job(struct input *in, int idle_ms, int output_fd, int done_fd);

#endif
