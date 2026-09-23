/* One way out of everything the launcher starts: Home + START.
 *
 * Emulators each had their own quit combo, or none - RetroArch its hotkeys,
 * ARMSX2 and PortMaster gptokeyb, mpv its input.conf, Steam and xemu
 * whatever they do. The launcher starts all of them, so it is the one place
 * that can give them the same exit: while a child has the panel it watches
 * the pads for Home + START, and when it sees it, it gives the program a
 * moment to leave by itself, then asks it to with SIGTERM, and only then
 * kills it.
 *
 * Not runemu.sh itself: what it does after the emulator returns - fan
 * profile, GPU performance level, CPU threads - has to run, so the signals
 * go to the rest of its process group.
 */
#ifndef PL_QUIT_H
#define PL_QUIT_H

/* linux/input.h's values, spelled out so this file builds and is tested on
 * a machine without that header. */
#define QUIT_EV_KEY     0x01
#define QUIT_BTN_START  0x13b
#define QUIT_BTN_MODE   0x13c     /* Home, PS, Guide: the button in the middle */

#define QUIT_MAX_DEV    24

struct quit_combo {
	unsigned char home[QUIT_MAX_DEV];
	unsigned char start[QUIT_MAX_DEV];
};

void quit_reset(struct quit_combo *q);

/* Feeds one event from device dev. Returns 1 when it completes the combo:
 * a press of one of the two while the other is held, on the same pad - Home
 * on one controller and START on another is two people, not a command. */
int quit_feed(struct quit_combo *q, int dev, unsigned type, unsigned code, int value);

/* From one line of /proc/<pid>/stat: the process group, and the parent.
 * Returns 0, or -1 for a line that does not parse. The command name is in
 * parentheses and may itself contain spaces and parentheses, so the fields
 * are counted from the last ')'. */
int quit_stat_pgrp(const char *stat, int *pgrp, int *ppid);

/* Sends sig to every process in process group pgrp except keep. Returns how
 * many were signalled. */
int quit_signal_group(int pgrp, int keep, int sig);

#endif
