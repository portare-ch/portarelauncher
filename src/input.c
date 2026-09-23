#include "input.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
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

int input_open(struct input *in)
{
	memset(in, 0, sizeof(*in));
	in->aux_fd = -1;

	DIR *d = opendir("/dev/input");
	if (!d) {
		fprintf(stderr, "opendir /dev/input: %s\n", strerror(errno));
		return -1;
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

	if (in->n == 0) {
		fprintf(stderr, "no usable input devices\n");
		return -1;
	}
	return 0;
}

void input_set_aux(struct input *in, int fd)
{
	in->aux_fd = fd;
}

void input_close(struct input *in)
{
	for (int i = 0; i < in->n; i++)
		close(in->fd[i]);
	in->n = 0;
}

static enum action from_key(unsigned code)
{
	switch (code) {
	/* By position, always. The bottom button confirms and the right one
	 * goes back on every pad this will see - B and A on a Retroid, cross
	 * and circle on a Sony. What changes between them is the printing, so
	 * the button style setting changes the labels the UI shows and not
	 * what any button does. */
	case BTN_SOUTH:  return ACT_CONFIRM;
	case BTN_EAST:   return ACT_BACK;
	case BTN_NORTH:  return ACT_ALT;
	case BTN_WEST:   return ACT_MENU;
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
	struct pollfd pfd[INPUT_MAX_DEV + 1];
	for (int i = 0; i < in->n; i++) {
		pfd[i].fd = in->fd[i];
		pfd[i].events = POLLIN;
		pfd[i].revents = 0;
	}
	int nfd = in->n;
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
