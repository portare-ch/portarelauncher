#include "input.h"
#include "quit.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* Key repeat, matching what a menu feels like rather than what a text field
 * needs: a pause before the first repeat, then steady. */
#define REPEAT_DELAY_MS 380
#define REPEAT_RATE_MS  90

/* Sticks rest near zero but not at it. Anything inside the deadzone is
 * centre; the axis has to return there before it will fire again. */
#define STICK_EDGE 16000

static int has_keys(int fd)
{
	unsigned long bits[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))];
	memset(bits, 0, sizeof(bits));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0)
		return 0;
	for (size_t i = 0; i < sizeof(bits) / sizeof(bits[0]); i++)
		if (bits[i])
			return 1;
	return 0;
}

/* Every event device that reports keys, opened fresh. */
static void open_devices(struct input *in)
{
	in->n = 0;
	DIR *d = opendir("/dev/input");
	if (!d) {
		fprintf(stderr, "opendir /dev/input: %s\n", strerror(errno));
		return;
	}

	struct dirent *e;
	while ((e = readdir(d)) && in->n < INPUT_MAX_DEV) {
		if (strncmp(e->d_name, "event", 5) != 0)
			continue;
		char path[280];
		snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (!has_keys(fd)) {
			close(fd);
			continue;
		}

		in->fd[in->n++] = fd;
	}
	closedir(d);

	if (in->quit_only)
		input_quit_only(in, 1);
}

static void close_devices(struct input *in)
{
	for (int i = 0; i < in->n; i++)
		close(in->fd[i]);
	in->n = 0;
}

int input_open(struct input *in)
{
	memset(in, 0, sizeof(*in));
	in->aux_fd = -1;

	in->notify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (in->notify_fd >= 0 &&
	    inotify_add_watch(in->notify_fd, "/dev/input", IN_CREATE | IN_DELETE) < 0) {
		close(in->notify_fd);
		in->notify_fd = -1;
	}

	open_devices(in);
	if (in->n == 0) {
		fprintf(stderr, "no usable input devices\n");
		return -1;
	}
	return 0;
}

int input_fill_poll(struct input *in, struct pollfd *pfd)
{
	int n = 0;
	for (int i = 0; i < in->n; i++)
		pfd[n++] = (struct pollfd){ .fd = in->fd[i], .events = POLLIN };
	if (in->notify_fd >= 0)
		pfd[n++] = (struct pollfd){ .fd = in->notify_fd, .events = POLLIN };
	return n;
}

int input_refresh(struct input *in, const struct pollfd *pfd, int n)
{
	int changed = 0;
	for (int i = 0; i < n; i++) {
		if (pfd[i].fd == in->notify_fd && (pfd[i].revents & POLLIN)) {
			/* Only that something changed matters, not what. */
			char buf[4096];
			while (read(in->notify_fd, buf, sizeof(buf)) > 0)
				;
			changed = 1;
		} else if (pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			/* A device that has gone. Polling it again returns at
			 * once, forever: that was the launcher at 100% of a core
			 * after every game. */
			changed = 1;
		}
	}
	if (!changed)
		return 0;

	close_devices(in);
	open_devices(in);
	in->held = ACT_NONE;
	in->repeats = 0;
	return 1;
}

void input_set_aux(struct input *in, int fd)
{
	in->aux_fd = fd;
}

void input_drain(struct input *in)
{
	struct input_event ev[64];
	for (int i = 0; i < in->n; i++)
		while (read(in->fd[i], ev, sizeof(ev)) > 0)
			;
	in->held = ACT_NONE;
	in->repeats = 0;
}

void input_close(struct input *in)
{
	close_devices(in);
	if (in->notify_fd >= 0)
		close(in->notify_fd);
	in->notify_fd = -1;
}

static int retroid_layout = 1;

void input_set_layout(int retroid)
{
	retroid_layout = retroid;
}

static enum action from_key(unsigned code)
{
	switch (code) {
	/* The four face buttons by role. The top one opens settings and the
	 * left one is the keyboard's shift whatever the style: X and Y on a
	 * Retroid, triangle and square on a PS pad. The style decides the
	 * other two. Retroid: A on the right confirms, B at the bottom goes
	 * back. PS: cross at the bottom confirms, circle on the right goes
	 * back. */
	case BTN_SOUTH:  return retroid_layout ? ACT_BACK    : ACT_CONFIRM;
	case BTN_EAST:   return retroid_layout ? ACT_CONFIRM : ACT_BACK;
	case BTN_NORTH:  return ACT_MENU;
	case BTN_WEST:   return ACT_ALT;
	/* Their own actions rather than aliases, because the keyboard needs
	 * START to mean "done" while the left button types a space. Screens
	 * that have no use for the difference fold START back into MENU. */
	case BTN_START:  return ACT_START;
	case BTN_SELECT: return ACT_SELECT;

	case BTN_DPAD_UP:    return ACT_UP;
	case BTN_DPAD_DOWN:  return ACT_DOWN;
	case BTN_DPAD_LEFT:  return ACT_LEFT;
	case BTN_DPAD_RIGHT: return ACT_RIGHT;

	/* A keyboard, for working on this over ssh with a USB keyboard
	 * plugged in, and as the only way out that does not need a pad. */
	case KEY_UP:     return ACT_UP;
	case KEY_DOWN:   return ACT_DOWN;
	case KEY_LEFT:   return ACT_LEFT;
	case KEY_RIGHT:  return ACT_RIGHT;
	case KEY_ENTER:  return ACT_CONFIRM;
	case KEY_SPACE:  return ACT_CONFIRM;
	case KEY_BACKSPACE: return ACT_BACK;
	case KEY_TAB:    return ACT_MENU;
	case KEY_ESC:    return ACT_QUIT;
	default:         return ACT_NONE;
	}
}

static int is_direction(enum action a)
{
	return a == ACT_UP || a == ACT_DOWN || a == ACT_LEFT || a == ACT_RIGHT;
}

/* Hats and the left stick both resolve to a direction or to centre. */
static enum action from_abs(unsigned code, int value)
{
	switch (code) {
	case ABS_HAT0X:
	case ABS_X:
		if (code == ABS_X && value > -STICK_EDGE && value < STICK_EDGE)
			return ACT_NONE;
		if (code == ABS_HAT0X && value == 0)
			return ACT_NONE;
		return value < 0 ? ACT_LEFT : ACT_RIGHT;
	case ABS_HAT0Y:
	case ABS_Y:
		if (code == ABS_Y && value > -STICK_EDGE && value < STICK_EDGE)
			return ACT_NONE;
		if (code == ABS_HAT0Y && value == 0)
			return ACT_NONE;
		return value < 0 ? ACT_UP : ACT_DOWN;
	default:
		return ACT_NONE;
	}
}

enum action input_wait(struct input *in, int idle_ms)
{
	struct pollfd pfd[INPUT_MAX_DEV + 2];
	int ndev = input_fill_poll(in, pfd);
	int nfd = ndev;
	if (in->aux_fd >= 0) {
		pfd[nfd].fd = in->aux_fd;
		pfd[nfd].events = POLLIN;
		pfd[nfd].revents = 0;
		nfd++;
	}

	/* Whichever comes first: the caller's refresh, or the next repeat of a
	 * held direction. */
	int timeout = idle_ms;
	if (in->held != ACT_NONE) {
		int rep = in->repeats == 0 ? REPEAT_DELAY_MS : REPEAT_RATE_MS;
		if (timeout < 0 || rep < timeout)
			timeout = rep;
	}

	int n = poll(pfd, (nfds_t)nfd, timeout);
	if (n < 0)
		return ACT_NONE;
	if (n == 0) {
		if (in->held != ACT_NONE) {
			in->repeats++;
			return in->held;
		}
		return ACT_TICK;
	}

	if (in->aux_fd >= 0 && (pfd[nfd - 1].revents & POLLIN))
		return ACT_AUX;

	if (input_refresh(in, pfd, ndev))
		return ACT_NONE;

	enum action out = ACT_NONE;
	for (int i = 0; i < in->n; i++) {
		if (!(pfd[i].revents & POLLIN))
			continue;
		struct input_event ev;
		while (read(in->fd[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
			enum action a = ACT_NONE;
			if (ev.type == EV_KEY) {
				a = from_key(ev.code);
				if (a == ACT_NONE)
					continue;
				if (ev.value == 0) {          /* release */
					if (is_direction(a) && in->held == a) {
						in->held = ACT_NONE;
						in->repeats = 0;
					}
					continue;
				}
				if (ev.value == 2)            /* kernel autorepeat */
					continue;                 /* we do our own    */
			} else if (ev.type == EV_ABS) {
				a = from_abs(ev.code, ev.value);
				if (a == ACT_NONE) {
					if (is_direction(in->held)) {
						in->held = ACT_NONE;
						in->repeats = 0;
					}
					continue;
				}
				if (a == in->held)            /* same direction, no edge */
					continue;
			} else {
				continue;
			}

			if (is_direction(a)) {
				in->held = a;
				in->repeats = 0;
			}
			out = a;
		}
	}
	return out;
}

_Static_assert(QUIT_BTN_MODE == BTN_MODE && QUIT_BTN_START == BTN_START &&
               QUIT_EV_KEY == EV_KEY, "quit.h spells out linux/input.h's codes");
_Static_assert(QUIT_MAX_DEV >= INPUT_MAX_DEV, "one combo slot per device");

static void set_bit(unsigned char *bits, unsigned n)
{
	bits[n / 8] |= (unsigned char)(1u << (n % 8));
}

void input_quit_only(struct input *in, int on)
{
	in->quit_only = on;
#ifdef EVIOCSMASK
	/* Two masks per device: which event types, then which key codes. Not
	 * even EV_SYN in the narrow one - a SYN_REPORT follows every stick
	 * report, and waking for those is exactly what this avoids. */
	unsigned char types[EV_CNT / 8 + 1], keys[KEY_CNT / 8 + 1];
	memset(types, on ? 0 : 0xff, sizeof(types));
	memset(keys, on ? 0 : 0xff, sizeof(keys));
	if (on) {
		set_bit(types, EV_KEY);
		set_bit(keys, BTN_MODE);
		set_bit(keys, BTN_START);
	}
	struct input_mask tm = { .type = EV_SYN, .codes_size = sizeof(types),
	                         .codes_ptr = (unsigned long)types };
	struct input_mask km = { .type = EV_KEY, .codes_size = sizeof(keys),
	                         .codes_ptr = (unsigned long)keys };
	for (int i = 0; i < in->n; i++) {
		ioctl(in->fd[i], EVIOCSMASK, &tm);
		ioctl(in->fd[i], EVIOCSMASK, &km);
	}
#else
	(void)in;
	(void)on;
#endif
}

int input_quit_read(struct input *in, struct quit_combo *q)
{
	int hit = 0;
	struct input_event ev;
	for (int i = 0; i < in->n; i++)
		while (read(in->fd[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev))
			if (quit_feed(q, i, ev.type, ev.code, ev.value))
				hit = 1;
	return hit;
}
