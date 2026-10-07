/* PortScope: every button and stick of the pad, as the kernel reports it.
 *
 * Started by the launcher from Settings > Diagnostics, with the panel
 * handed over the way a game gets it. One more launcher screen on the
 * launcher's own pieces: kms.c, the 8x16 grid, the palette. Two things
 * the font cannot draw are drawn in pixels: a round ring per stick with
 * a dot at the panel's resolution, and the shape marks as outlines.
 *
 * One layer at a time. By default the virtual pad InputPlumber presents,
 * which is what games read. Holding SELECT for a second switches to the
 * MCU's own device and the gpio-keys, and back. InputPlumber grabs those
 * (EVIOCGRAB), so a second reader gets nothing while it runs: the raw
 * layer stops InputPlumber, and it is started again when the layer is
 * left and whenever this program ends. The launcher starts it again as
 * well, for the one way out that runs no code here, SIGKILL.
 *
 * Home + START leaves, the combo that leaves everything on the device.
 */
#include "kms.h"
#include "pix.h"
#include "quit.h"
#include "scope.h"
#include "settings.h"
#include "term.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

_Static_assert(SC_BTN_SOUTH == BTN_SOUTH && SC_BTN_NORTH == BTN_NORTH &&
               SC_BTN_WEST == BTN_WEST && SC_BTN_C == BTN_C &&
               SC_BTN_Z == BTN_Z && SC_BTN_TR2 == BTN_TR2 &&
               SC_BTN_MODE == BTN_MODE && SC_BTN_THUMBR == BTN_THUMBR &&
               SC_BTN_BACK == BTN_BACK && SC_KEY_ESC == KEY_ESC &&
               SC_BTN_DPAD_UP == BTN_DPAD_UP &&
               SC_BTN_DPAD_RIGHT == BTN_DPAD_RIGHT &&
               SC_BTN_TRIGGER_HAPPY3 == BTN_TRIGGER_HAPPY3 &&
               SC_BTN_TRIGGER_HAPPY4 == BTN_TRIGGER_HAPPY4 &&
               SC_ABS_RZ == ABS_RZ && SC_ABS_HAT0Y == ABS_HAT0Y &&
               SC_EV_ABS == EV_ABS && SC_SYN_REPORT == SYN_REPORT,
               "scope.h spells out linux/input.h's codes");

extern char **environ;

#define CARD        "/dev/dri/card0"
#define SETTINGS    "/storage/.config/system/configs/system.cfg"
#define INPUTPLUMBER "inputplumber.service"
#define SCALE       3
#define MAX_DEV     16
#define HOLD_MS     1000     /* SELECT held this long switches the layer   */
#define FRAME_MS    8        /* the panel's 120 Hz, near enough            */
#define SETTLE_MS   5000     /* for the devices of a new layer to appear   */

static volatile sig_atomic_t stop_requested;
static void on_signal(int sig) { (void)sig; stop_requested = 1; }

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* ---- devices ----------------------------------------------------------- */

struct devs {
	int fd[MAX_DEV];
	int pad[MAX_DEV];    /* has BTN_SOUTH: counted for the rate */
	int n, npads;
	int notify;
};

#define LONGS(n) (((n) + 8 * sizeof(long) - 1) / (8 * sizeof(long)))

static int has_bit(const unsigned long *b, unsigned n)
{
	return (b[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
}

static void close_devs(struct devs *d)
{
	for (int i = 0; i < d->n; i++)
		close(d->fd[i]);
	d->n = d->npads = 0;
}

/* Which devices a layer is made of. A pad is anything with BTN_SOUTH: the
 * virtual pad while InputPlumber runs, the MCU once it does not. The raw
 * layer adds the device that carries the paddles, gpio-keys, and so takes
 * whatever reports BTN_Z or BTN_C as well. Ranges and rest positions come
 * from the first pad. */
static void open_devs(struct devs *d, struct scope *s)
{
	close_devs(d);
	DIR *dir = opendir("/dev/input");
	if (!dir)
		return;

	int num[MAX_DEV * 2];
	int nn = 0;
	struct dirent *e;
	while ((e = readdir(dir)) && nn < MAX_DEV * 2)
		if (strncmp(e->d_name, "event", 5) == 0)
			num[nn++] = atoi(e->d_name + 5);
	closedir(dir);
	/* In event number order, so "the first pad" is the same one each time. */
	for (int i = 1; i < nn; i++)
		for (int j = i; j > 0 && num[j] < num[j - 1]; j--) {
			int tmp = num[j];
			num[j] = num[j - 1];
			num[j - 1] = tmp;
		}

	for (int i = 0; i < nn && d->n < MAX_DEV; i++) {
		char path[32];
		snprintf(path, sizeof(path), "/dev/input/event%d", num[i]);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		unsigned long keys[LONGS(KEY_CNT)] = { 0 };
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		int pad = has_bit(keys, BTN_SOUTH);
		int paddles = has_bit(keys, BTN_Z) || has_bit(keys, BTN_C);
		if (!pad && !(s->layer == SCOPE_RAW && paddles)) {
			close(fd);
			continue;
		}
		int clk = CLOCK_MONOTONIC;
		ioctl(fd, EVIOCSCLOCKID, &clk);

		if (pad && d->npads == 0) {
			static const unsigned axes[] = { ABS_X, ABS_Y, ABS_RX, ABS_RY,
			                                 ABS_Z, ABS_RZ };
			for (size_t k = 0; k < sizeof(axes) / sizeof(axes[0]); k++) {
				struct input_absinfo ai;
				if (ioctl(fd, EVIOCGABS(axes[k]), &ai) == 0)
					scope_range(s, axes[k], ai.minimum, ai.maximum, ai.value);
			}
		}
		d->pad[d->n] = pad;
		d->fd[d->n++] = fd;
		d->npads += pad;
	}
}

static int systemctl(const char *verb)
{
	char *const argv[] = { (char *)"systemctl", (char *)verb,
	                       (char *)INPUTPLUMBER, NULL };
	pid_t pid;
	if (posix_spawnp(&pid, "systemctl", NULL, NULL, argv, environ) != 0)
		return -1;
	int st;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
		;
	return WIFEXITED(st) && WEXITSTATUS(st) == 0 ? 0 : -1;
}

/* Waits for the layer's pad to show up: InputPlumber takes a moment to
 * create its virtual pad, and to give the MCU's node back when it stops. */
static void settle(struct devs *d, struct scope *s)
{
	long long until = now_ms() + SETTLE_MS;
	for (;;) {
		open_devs(d, s);
		if (d->npads > 0 || now_ms() >= until || stop_requested)
			return;
		struct pollfd p = { .fd = d->notify, .events = POLLIN };
		if (poll(&p, 1, 100) > 0) {
			char buf[4096];
			while (read(d->notify, buf, sizeof(buf)) > 0)
				;
		}
	}
}

static void set_layer(struct devs *d, struct scope *s, enum scope_layer layer)
{
	close_devs(d);
	if (layer == SCOPE_RAW && systemctl("stop") < 0)
		fprintf(stderr, "portscope: could not stop %s\n", INPUTPLUMBER);
	if (layer == SCOPE_VIRTUAL && systemctl("start") < 0)
		fprintf(stderr, "portscope: could not start %s\n", INPUTPLUMBER);
	scope_reset(s, layer);
	settle(d, s);
}

/* ---- drawing ----------------------------------------------------------- */

struct view {
	struct term *t;
	struct pix px;
	int shapes;          /* launcher.buttons: the shape marks, not letters */
	/* What the pixel layer last drew, so only what changed is redrawn. */
	int valid;
	int dot_x[2], dot_y[2], ring_lit[2];
	int mark_lit[4];
	int shown_rate;
};

/* Rounded as printed, so a stick resting a hair left of centre reads
 * +0.00 like one a hair right of it, not -0.00. */
static double shown(double v)
{
	double r = round(v * 100) / 100;
	return r == 0 ? 0 : r;
}

static int attr_of(const struct scope *s, enum scope_ctl c)
{
	return scope_lit(s, c) ? ATTR_BRIGHT : ATTR_DIM;
}

static void label(struct view *v, const struct scope *s, unsigned x, unsigned y,
                  const char *text, enum scope_ctl c)
{
	term_puts(v->t, x, y, text, attr_of(s, c));
}

static void glyph(struct view *v, const struct scope *s, unsigned x, unsigned y,
                  unsigned char g, enum scope_ctl c)
{
	term_putc(v->t, x, y, g, attr_of(s, c));
}

/* The face buttons, by position: top, left, right, bottom. */
static const enum scope_ctl face_ctl[4] = { CTL_TOP, CTL_WEST, CTL_EAST, CTL_BOTTOM };
static const unsigned face_x[4] = { 40, 37, 43, 40 };
static const unsigned face_y[4] = { 5, 6, 6, 7 };
/* What a Retroid has printed on them. */
static const char face_letter[4] = { 'X', 'Y', 'A', 'B' };

static void draw_text(struct view *v, const struct scope *s, long long now)
{
	struct term *t = v->t;
	char buf[64];

	term_clear(t);
	term_puts(t, 1, 0, s->layer == SCOPE_RAW ? "PortScope, raw" : "PortScope",
	          ATTR_TEXT);
	v->shown_rate = scope_rate(s, now);
	if (v->shown_rate > 0)
		snprintf(buf, sizeof(buf), "RATE %d Hz", v->shown_rate);
	else
		snprintf(buf, sizeof(buf), "RATE -- Hz");
	term_puts_right(t, t->cols - 1, 0, buf, ATTR_MID);
	term_hline(t, 1, G_HLINE_D, ATTR_DIM);

	label(v, s, 2, 2, "L1", CTL_L1);
	label(v, s, 49, 2, "R1", CTL_R1);
	label(v, s, 2, 3, "L2", CTL_L2);
	snprintf(buf, sizeof(buf), "%.2f", shown(scope_axis(s, AX_LT)));
	label(v, s, 6, 3, buf, CTL_L2);
	snprintf(buf, sizeof(buf), "%.2f", shown(scope_axis(s, AX_RT)));
	label(v, s, 43, 3, buf, CTL_R2);
	label(v, s, 49, 3, "R2", CTL_R2);
	label(v, s, 2, 4, "L3", CTL_L3);
	label(v, s, 49, 4, "R3", CTL_R3);

	/* CP437 arrows and the middle dot. */
	glyph(v, s, 10, 5, 0x18, CTL_UP);
	glyph(v, s, 7, 6, 0x1B, CTL_LEFT);
	term_putc(t, 10, 6, 0xFA, ATTR_DIM);
	glyph(v, s, 13, 6, 0x1A, CTL_RIGHT);
	glyph(v, s, 10, 7, 0x19, CTL_DOWN);

	if (!v->shapes)
		for (int i = 0; i < 4; i++)
			glyph(v, s, face_x[i], face_y[i], (unsigned char)face_letter[i],
			      face_ctl[i]);

	snprintf(buf, sizeof(buf), "X %+.2f Y %+.2f",
	         shown(scope_axis(s, AX_LX)), shown(scope_axis(s, AX_LY)));
	term_puts(t, 5, 14, buf, ATTR_TEXT);
	snprintf(buf, sizeof(buf), "X %+.2f Y %+.2f",
	         shown(scope_axis(s, AX_RX)), shown(scope_axis(s, AX_RY)));
	term_puts_right(t, t->cols - 1, 14, buf, ATTR_TEXT);

	label(v, s, 12, 15, "SELECT", CTL_SELECT);
	label(v, s, 23, 15, "HOME", CTL_HOME);
	label(v, s, 32, 15, "START", CTL_START);
	label(v, s, 19, 16, "M1", CTL_M1);
	label(v, s, 28, 16, "M2", CTL_M2);

	term_puts(t, 1, 17, "Last input", ATTR_TEXT);
	for (int i = 0; i < s->nlast; i++) {
		char nb[24];
		const char *name = scope_code_name(s->last[i].type, s->last[i].code,
		                                   nb, sizeof(nb));
		if (strlen(name) <= 10)
			snprintf(buf, sizeof(buf), "%-10s%6d", name, s->last[i].value);
		else
			snprintf(buf, sizeof(buf), "%s %d", name, s->last[i].value);
		term_puts(t, 1, (unsigned)(18 + i), buf, ATTR_TEXT);
	}
}

static void draw_pixels(struct view *v, const struct scope *s)
{
	struct term *t = v->t;
	const int cw = 8 * (int)t->scale, ch = 16 * (int)t->scale;
	const uint32_t bg = term_color(t, ATTR_BG);
	const uint32_t dim = term_color(t, ATTR_DIM);
	const uint32_t bright = term_color(t, ATTR_BRIGHT);

	/* The sticks: five rows tall, centred on row 11, under the D-pad and
	 * the face buttons. The dot travels inside the ring and never
	 * reaches it, so moving the dot never has to repaint the ring. */
	static const double ring_col[2] = { 9.5, 41.5 };
	const int r = 5 * ch / 2 - 8, dot = 8, travel = r - 12;
	for (int i = 0; i < 2; i++) {
		int cx = (int)t->ox + (int)(ring_col[i] * cw);
		int cy = (int)t->oy + 11 * ch + ch / 2;
		double x = scope_axis(s, i ? AX_RX : AX_LX);
		double y = scope_axis(s, i ? AX_RY : AX_LY);
		double m = x * x + y * y;
		if (m > 1) {             /* a square gate reads 1,1 in a corner */
			double k = 1 / sqrt(m);
			x *= k;
			y *= k;
		}
		int dx = cx + (int)(x * travel + (x < 0 ? -0.5 : 0.5));
		int dy = cy + (int)(y * travel + (y < 0 ? -0.5 : 0.5));
		int lit = scope_lit(s, i ? CTL_R3 : CTL_L3);

		if (!v->valid || v->ring_lit[i] != lit) {
			pix_ring(&v->px, cx, cy, r, 2, lit ? bright : dim);
			v->ring_lit[i] = lit;
		}
		if (!v->valid || v->dot_x[i] != dx || v->dot_y[i] != dy) {
			if (v->valid)
				pix_disc(&v->px, v->dot_x[i], v->dot_y[i], dot, bg);
			pix_disc(&v->px, dx, dy, dot, bright);
			v->dot_x[i] = dx;
			v->dot_y[i] = dy;
		}
	}

	if (v->shapes) {
		for (int i = 0; i < 4; i++) {
			int lit = scope_lit(s, face_ctl[i]);
			if (v->valid && v->mark_lit[i] == lit)
				continue;
			int x0 = (int)t->ox + (int)face_x[i] * cw;
			int y0 = (int)t->oy + (int)face_y[i] * ch;
			int cx = x0 + cw / 2, cy = y0 + ch / 2, sz = 11;
			uint32_t c = lit ? bright : dim;
			pix_fill(&v->px, x0, y0, cw, ch, bg);
			switch (face_ctl[i]) {
			case CTL_TOP:    pix_triangle(&v->px, cx, cy, sz, 2, c); break;
			case CTL_WEST:   pix_square(&v->px, cx, cy, sz, 2, c);   break;
			case CTL_EAST:   pix_circle(&v->px, cx, cy, sz, 2, c);   break;
			default:         pix_cross(&v->px, cx, cy, sz, 2, c);    break;
			}
			v->mark_lit[i] = lit;
		}
	}
	v->valid = 1;
}

static void draw(struct view *v, const struct scope *s, long long now)
{
	draw_text(v, s, now);
	/* Cells first: a cell repainted after the pixels would black out
	 * whatever was drawn across it. The ring rows and the mark cells stay
	 * blank in the grid, so only a full repaint touches them, and that
	 * clears v->valid. */
	term_flush(v->t);
	draw_pixels(v, s);
}

/* ---- main -------------------------------------------------------------- */

static void use_palette(struct term *t)
{
	char want[32];
	term_set_palette(t, 0);
	if (!settings_get(SETTINGS, "launcher.palette", want, sizeof(want)))
		return;
	for (int i = 0; i < term_palette_count(); i++)
		if (strcmp(term_set_palette(t, i), want) == 0)
			return;
	term_set_palette(t, 0);
}

static int use_shapes(void)
{
	char style[32];
	return settings_get(SETTINGS, "launcher.buttons", style, sizeof(style)) &&
	       (strcmp(style, "shapes") == 0 || strcmp(style, "ps") == 0 ||
	        strcmp(style, "sony") == 0);
}

int main(int argc, char **argv)
{
	enum scope_layer start = SCOPE_VIRTUAL;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--raw") == 0)
			start = SCOPE_RAW;
		else {
			fprintf(stderr, "usage: portscope [--raw]\n");
			return 2;
		}
	}

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = on_signal;    /* no SA_RESTART: poll has to return */
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	struct kms kms;
	if (kms_open(&kms, CARD) < 0)
		return 1;
	struct term term;
	memset(&term, 0, sizeof(term));
	if (term_init(&term, kms.map, kms.pitch_px, kms.mode.hdisplay,
	              kms.mode.vdisplay, SCALE) < 0) {
		kms_close(&kms);
		return 1;
	}
	use_palette(&term);

	struct view v;
	memset(&v, 0, sizeof(v));
	v.t = &term;
	v.px = (struct pix){ kms.map, kms.pitch_px, kms.mode.hdisplay, kms.mode.vdisplay };
	v.shapes = use_shapes();

	static struct scope s;
	struct devs d;
	memset(&d, 0, sizeof(d));
	d.notify = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (d.notify >= 0)
		inotify_add_watch(d.notify, "/dev/input", IN_CREATE | IN_DELETE | IN_ATTRIB);

	scope_reset(&s, start);
	draw(&v, &s, now_ms());
	if (start == SCOPE_RAW)
		set_layer(&d, &s, SCOPE_RAW);
	else
		settle(&d, &s);

	struct quit_combo q;
	quit_reset(&q);
	long long select_at = 0, drawn_at = 0;
	int dirty = 1;

	while (!stop_requested) {
		long long now = now_ms();
		if (select_at && now - select_at >= HOLD_MS) {
			select_at = 0;
			set_layer(&d, &s, s.layer == SCOPE_RAW ? SCOPE_VIRTUAL : SCOPE_RAW);
			quit_reset(&q);
			term_invalidate(&term);
			v.valid = 0;
			dirty = 1;
			continue;
		}
		if (scope_rate(&s, now) != v.shown_rate)
			dirty = 1;
		if (dirty && now - drawn_at >= FRAME_MS) {
			draw(&v, &s, now);
			drawn_at = now;
			dirty = 0;
		}

		/* Asleep unless something is due: the next frame, the end of a
		 * SELECT hold, or the rate falling back once the pad is still. */
		long long due = -1;
		if (dirty)
			due = drawn_at + FRAME_MS;
		if (select_at && (due < 0 || select_at + HOLD_MS < due))
			due = select_at + HOLD_MS;
		if (v.shown_rate > 0 && (due < 0 || now + 250 < due))
			due = now + 250;
		int timeout = due < 0 ? -1 : due <= now ? 0 : (int)(due - now);

		struct pollfd pfd[MAX_DEV + 1];
		int n = 0;
		for (int i = 0; i < d.n; i++)
			pfd[n++] = (struct pollfd){ .fd = d.fd[i], .events = POLLIN };
		if (d.notify >= 0)
			pfd[n++] = (struct pollfd){ .fd = d.notify, .events = POLLIN };
		if (poll(pfd, (nfds_t)n, timeout) <= 0)
			continue;

		int reopen = 0;
		if (d.notify >= 0 && (pfd[n - 1].revents & POLLIN)) {
			char buf[4096];
			while (read(d.notify, buf, sizeof(buf)) > 0)
				;
			reopen = 1;
		}
		for (int i = 0; i < d.n && !stop_requested; i++) {
			if (pfd[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				reopen = 1;
				continue;
			}
			if (!(pfd[i].revents & POLLIN))
				continue;
			struct input_event ev;
			while (read(d.fd[i], &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
				if (quit_feed(&q, i, ev.type, ev.code, ev.value) ||
				    (ev.type == EV_KEY && ev.code == KEY_ESC && ev.value == 1)) {
					stop_requested = 1;
					break;
				}
				if (ev.type == EV_SYN && !d.pad[i])
					continue;     /* the rate is the pad's */
				long long t = (long long)ev.input_event_sec * 1000 +
				              ev.input_event_usec / 1000;
				dirty |= scope_feed(&s, ev.type, ev.code, ev.value, t);
				if (ev.type == EV_KEY && ev.code == BTN_SELECT && ev.value != 2)
					select_at = ev.value ? now_ms() : 0;
			}
		}
		if (reopen && !stop_requested) {
			/* A pad that went or came: InputPlumber restarting, a
			 * Bluetooth pad. What is held is forgotten with it. */
			enum scope_layer l = s.layer;
			scope_reset(&s, l);
			open_devs(&d, &s);
			quit_reset(&q);
			select_at = 0;
			dirty = 1;
		}
	}

	close_devs(&d);
	if (s.layer == SCOPE_RAW && systemctl("start") < 0)
		fprintf(stderr, "portscope: could not start %s\n", INPUTPLUMBER);
	if (d.notify >= 0)
		close(d.notify);
	term_free(&term);
	kms_close(&kms);
	return 0;
}
