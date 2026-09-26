/* Gamepad and keyboard, straight from evdev.
 *
 * Every /dev/input/event* that reports keys is opened and polled together,
 * so the built-in pad, a paired controller and a USB keyboard all work
 * without knowing which is which.
 *
 * The poll blocks with no timeout while nothing is held, which is where the
 * idle cost of this program goes to zero. A timeout is only used to drive
 * key repeat for a held direction.
 */
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
	ACT_MENU,     /* the settings key            */
	ACT_ALT,      /* L1: shift on the keyboard, nothing elsewhere */
	ACT_HOME,     /* Home: the consoles list, from anywhere */
	ACT_START,    /* START: "open settings" everywhere except the keyboard */
	ACT_SELECT,   /* SELECT: only the keyboard uses it, to hide the text */
	ACT_TICK,     /* the idle timeout expired; nothing was pressed */
	ACT_AUX,      /* the auxiliary fd has something to read */
	ACT_QUIT,     /* only bound to a keyboard escape hatch */
};

#define INPUT_MAX_DEV 24

struct input {
	int fd[INPUT_MAX_DEV];
	int n;
	/* Something that is not an input device but has to wake the same
	 * poll: the OSD pipe. Kept separate so its bytes are never parsed as
	 * evdev events. */
	int aux_fd;
	/* inotify on /dev/input. InputPlumber recreates its virtual pad
	 * whenever it restarts - which happens at the end of a game - and a
	 * Bluetooth pad comes and goes; either way the devices are reopened
	 * rather than read through descriptors whose device is gone. */
	int notify_fd;
	int quit_only;         /* the mask input_quit_only set, reapplied */
	enum action held;      /* direction currently held, for repeat */
	int repeats;           /* how many repeats have fired          */
};

/* Clears *in before opening anything, so input_set_aux comes after it. */
int  input_open(struct input *in);

/* For a caller with a poll loop of its own: adds the devices and the
 * hotplug watch to pfd (room for INPUT_MAX_DEV + 1) and returns how many.
 * After poll, input_refresh reopens the devices if one has gone or appeared
 * and returns 1 when it did; the caller then reads nothing from this round,
 * since the descriptors it polled are closed. */
struct pollfd;
int  input_fill_poll(struct input *in, struct pollfd *pfd);
int  input_refresh(struct input *in, const struct pollfd *pfd, int n);
void input_set_aux(struct input *in, int fd);
void input_close(struct input *in);

/* Which face button does what. The top button opens settings in both
 * styles. retroid=1: the right button confirms and the bottom one goes
 * back. retroid=0, the PS style: bottom confirms, right goes back. Takes
 * effect on the next press. */
void input_set_layout(int retroid);

/* Throws away everything the devices buffered while something else had the
 * panel. The launcher's descriptors stay open while an emulator or a tool
 * runs, and the kernel queues every press on them meanwhile - including
 * the combo that quit the thing, which would otherwise arrive the moment
 * the launcher is back: START+SELECT out of the gamepad tester, replayed,
 * opens Settings. */
void input_drain(struct input *in);

/* While another program has the panel, the launcher only listens for the quit
 * combo (quit.h). on=1 asks the kernel to deliver nothing but Home and START
 * key events on every device, so a game's stick movements do not wake this
 * program hundreds of times a second; on=0 puts everything back. A kernel
 * without EVIOCSMASK just delivers everything, which costs wake-ups but
 * works. */
void input_quit_only(struct input *in, int on);

/* Reads whatever is waiting and returns 1 if it completed Home + START. */
struct quit_combo;
int  input_quit_read(struct input *in, struct quit_combo *q);

/* Blocks until something happens and returns one action.
 *
 * idle_ms bounds the wait so the caller can refresh a clock; ACT_TICK says
 * that is why it returned. A negative idle_ms blocks forever. Key repeat
 * shortens the wait on its own when a direction is held, so passing a long
 * idle_ms does not make the pad feel sluggish.
 *
 * ACT_NONE means it woke for something that is not a binding, which the
 * caller can ignore and call again. */
enum action input_wait(struct input *in, int idle_ms);

#endif
