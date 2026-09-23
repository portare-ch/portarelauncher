/* portarelauncher - a KMS launcher for PortareOS.
 *
 * Owns the panel, lists what there is to play, hands the display to an
 * emulator and takes it back. See README.md for why it looks like this.
 */
#include "bt.h"
#include "catalog.h"
#include "input.h"
#include "kms.h"
#include "net.h"
#include "osd.h"
#include "settings.h"
#include "status.h"
#include "term.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define CARD        "/dev/dri/card0"
#define ES_SYSTEMS  "/usr/config/emulationstation/es_systems.cfg"
#define SETTINGS    "/storage/.config/system/configs/system.cfg"
#define SCALE       3          /* 8x16 glyphs at 3x is 53x20 on this panel */

#define LIST_TOP    5
#define LIST_MARGIN 3          /* rows kept below the list for the footer  */

enum screen { SCR_SYSTEMS, SCR_GAMES, SCR_SETTINGS, SCR_WIFI, SCR_BT };

struct ui {
	struct term term;
	struct kms kms;
	struct input in;
	struct catalog cat;

	const char *pal_name;
	struct status st;
	struct osd osd;

	struct net_list nets;
	int wifi_sel, wifi_top;

	struct bt_list bt;
	int bt_on, bt_auto;
	int bt_sel, bt_top;
	char usb[24];
	char usb_opts[8][24];
	int n_usb;
	int retroid;         /* which printing the pad carries */
	enum screen screen;
	int sys_sel, sys_top;
	int game_sel, game_top;
	int set_sel;
	int running;
};

/* Which face button confirms. Stored in system.cfg like everything else,
 * so it survives a restart and is visible to the rest of the system. */
#define KEY_BUTTONS "launcher.buttons"

/* Sony's marks approximated out of CP437, which is all the VGA font has.
 * Close enough to be recognised, and not the real symbols. */
#define G_CIRCLE    0x09   /* O   */
#define G_TRIANGLE  0x1E   /* /\  */
#define G_SQUARE    0xFE   /* []  */

enum { SET_WIFI = 0, SET_BLUETOOTH, SET_USB, SET_BUTTONS, N_SETTINGS };

static const char *const settings_labels[N_SETTINGS] = {
	"Wi-Fi",
	"Bluetooth",
	"USB gadget mode",
	"Button style",
};

static volatile sig_atomic_t stop_requested;
static volatile sig_atomic_t blank_requested;   /* -1 off, 1 on, 0 nothing */

static void on_signal(int sig)
{
	switch (sig) {
	case SIGUSR1: blank_requested = -1; break;
	case SIGUSR2: blank_requested = 1;  break;
	default:      stop_requested = 1;   break;
	}
}

static unsigned list_rows(const struct ui *u)
{
	return u->term.rows - LIST_TOP - LIST_MARGIN;
}

/* Keeps the selected row inside the window without recentring on every
 * move, which is what makes a list feel steady rather than sliding. */
static void scroll_to(int sel, int *top, int count, int visible)
{
	if (count <= visible) {
		*top = 0;
		return;
	}
	if (sel < *top)
		*top = sel;
	else if (sel >= *top + visible)
		*top = sel - visible + 1;
	if (*top > count - visible)
		*top = count - visible;
	if (*top < 0)
		*top = 0;
}

static void draw_frame(struct ui *u, const char *crumb)
{
	struct term *t = &u->term;
	char right[64];

	term_clear(t);
	term_puts(t, 1, 0, crumb, ATTR_TEXT);

	/* Retro machines did not pop up overlays; they showed the state on the
	 * panel and left it there. Volume and brightness sit beside the
	 * battery for the same reason. */
	int n = 0;
	if (u->st.volume >= 0)
		n += snprintf(right + n, sizeof(right) - n, "VOL %d%%  ", u->st.volume);
	if (u->st.brightness >= 0)
		n += snprintf(right + n, sizeof(right) - n, "BRI %d%%  ", u->st.brightness);
	if (u->st.capacity >= 0)
		n += snprintf(right + n, sizeof(right) - n, "%s %d%%  ",
		              u->st.charging ? "CHG" : "BAT", u->st.capacity);
	snprintf(right + n, sizeof(right) - n, "%s", u->st.clock);
	term_puts_right(t, t->cols - 1, 0, right, ATTR_MID);

	term_hline(t, 1, G_HLINE_D, ATTR_DIM);
	term_hline(t, t->rows - 2, G_HLINE, ATTR_DIM);
}

/* What the four face buttons are called on the pad in the user's hands.
 * Position is fixed; only the printing differs. */
struct face {
	unsigned char bottom, right, top, left;
};

static struct face face_of(int retroid)
{
	struct face f;
	if (retroid) {
		f.bottom = 'B'; f.right = 'A'; f.top = 'X'; f.left = 'Y';
	} else {
		f.bottom = 'X'; f.right = G_CIRCLE;
		f.top = G_TRIANGLE; f.left = G_SQUARE;
	}
	return f;
}

static void draw_row(struct ui *u, unsigned y, int selected,
                     const char *label, const char *right)
{
	struct term *t = &u->term;
	int attr = selected ? ATTR_BRIGHT : ATTR_TEXT;

	if (selected)
		term_putc(t, 2, y, G_CARET, ATTR_BRIGHT);
	term_puts(t, 4, y, label, attr);
	if (right && *right)
		term_puts_right(t, t->cols - 2, y, right, selected ? ATTR_BRIGHT : ATTR_MID);
}

static void draw_systems(struct ui *u)
{
	struct term *t = &u->term;
	char buf[64];

	draw_frame(u, "PortareOS");

	snprintf(buf, sizeof(buf), "%d found", u->cat.n);
	term_puts(t, 2, 3, "SYSTEMS", ATTR_MID);
	term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);

	int visible = (int)list_rows(u);
	scroll_to(u->sys_sel, &u->sys_top, u->cat.n, visible);

	for (int i = 0; i < visible && u->sys_top + i < u->cat.n; i++) {
		const struct psystem *s = &u->cat.sys[u->sys_top + i];
		snprintf(buf, sizeof(buf), "%d", s->ngames);
		draw_row(u, (unsigned)(LIST_TOP + i), u->sys_top + i == u->sys_sel,
		         s->fullname[0] ? s->fullname : s->name, buf);
	}

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c SELECT   %c SETTINGS", f.bottom, f.left);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
	snprintf(buf, sizeof(buf), "%c COLOUR %s", f.top, u->pal_name);
	term_puts_right(t, t->cols - 1, t->rows - 1, buf, ATTR_DIM);
}

static void draw_games(struct ui *u)
{
	struct term *t = &u->term;
	const struct psystem *s = &u->cat.sys[u->sys_sel];
	char crumb[96], buf[64];

	snprintf(crumb, sizeof(crumb), "%s",
	         s->fullname[0] ? s->fullname : s->name);
	draw_frame(u, crumb);

	int visible = (int)list_rows(u);
	scroll_to(u->game_sel, &u->game_top, s->ngames, visible);

	for (int i = 0; i < visible && u->game_top + i < s->ngames; i++)
		draw_row(u, (unsigned)(LIST_TOP + i - 2),
		         u->game_top + i == u->game_sel,
		         s->games[u->game_top + i].name, NULL);

	snprintf(buf, sizeof(buf), "%d of %d", u->game_sel + 1, s->ngames);
	term_puts(t, 4, t->rows - 4, s->core[0] ? s->core : s->emulator, ATTR_MID);
	term_puts_right(t, t->cols - 2, t->rows - 4, buf, ATTR_MID);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c LAUNCH   %c BACK", f.bottom, f.right);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
	snprintf(buf, sizeof(buf), "%c COLOUR %s", f.top, u->pal_name);
	term_puts_right(t, t->cols - 1, t->rows - 1, buf, ATTR_DIM);
}

/* A diamond of the four face buttons, because the argument is about where
 * they are rather than what they are called. */
static void draw_face(struct ui *u, unsigned y, int retroid)
{
	struct term *t = &u->term;
	struct face f = face_of(retroid);

	/* The bottom button confirms whichever pad this is, so it is the
	 * bright one in both diagrams. Only its name changes. */
	term_putc(t, 9,  y,     f.top,    ATTR_TEXT);
	term_putc(t, 6,  y + 1, f.left,   ATTR_TEXT);
	term_putc(t, 12, y + 1, f.right,  ATTR_TEXT);
	term_putc(t, 9,  y + 2, f.bottom, ATTR_BRIGHT);

	term_putc(t, 20, y,     f.bottom, ATTR_BRIGHT);
	term_puts(t, 22, y,     "confirm", ATTR_MID);
	term_putc(t, 20, y + 1, f.right,  ATTR_TEXT);
	term_puts(t, 22, y + 1, "back", ATTR_MID);
}

static void draw_settings(struct ui *u)
{
	struct term *t = &u->term;

	draw_frame(u, "Settings");

	char val[64];
	for (int i = 0; i < N_SETTINGS; i++) {
		const char *value = "not wired up";
		switch (i) {
		case SET_WIFI: {
			const char *ssid = NULL;
			for (int k = 0; k < u->nets.n; k++)
				if (u->nets.e[k].active)
					ssid = u->nets.e[k].name;
			value = ssid ? ssid : (net_wifi_enabled() ? "not connected" : "off");
			break;
		}
		case SET_USB:
			snprintf(val, sizeof(val), "%s", u->usb[0] ? u->usb : "unknown");
			value = val;
			break;
		case SET_BLUETOOTH: {
			const char *dev = NULL;
			for (int k = 0; k < u->bt.n; k++)
				if (u->bt.d[k].connected)
					dev = u->bt.d[k].name;
			value = dev ? dev : (u->bt_on ? "no devices" : "off");
			break;
		}
		case SET_BUTTONS:
			value = u->retroid ? "Retroid" : "PS";
			break;
		}
		draw_row(u, (unsigned)(3 + i), i == u->set_sel,
		         settings_labels[i], value);
	}

	unsigned y = 3 + N_SETTINGS + 1;
	term_hline(t, y, G_HLINE, ATTR_DIM);

	switch (u->set_sel) {
	case SET_BUTTONS:
		draw_face(u, y + 2, u->retroid);
		break;
	case SET_WIFI: {
		char addr[40] = "";
		net_address(addr, sizeof(addr));
		term_puts(t, 4, y + 2, "Saved networks reconnect without a", ATTR_DIM);
		term_puts(t, 4, y + 3, "password. Open to pick one.", ATTR_DIM);
		if (addr[0]) {
			snprintf(val, sizeof(val), "address  %s", addr);
			term_puts(t, 4, y + 5, val, ATTR_MID);
		}
		break;
	}
	case SET_BLUETOOTH:
		term_puts(t, 4, y + 2, "Headphones, controllers. Open to", ATTR_DIM);
		term_puts(t, 4, y + 3, "scan, connect, set auto-connect.", ATTR_DIM);
		break;
	case SET_USB: {
		char addr[40] = "";
		usb_address(addr, sizeof(addr));
		term_puts(t, 4, y + 2, "network shares the link over USB,", ATTR_DIM);
		term_puts(t, 4, y + 3, "file_transfer exposes storage.", ATTR_DIM);
		if (addr[0] && strcmp(u->usb, "network") == 0) {
			snprintf(val, sizeof(val), "address  %s", addr);
			term_puts(t, 4, y + 5, val, ATTR_MID);
		}
		break;
	}
	default:
		term_puts(t, 4, y + 2, "Not implemented yet.", ATTR_DIM);
		break;
	}

	{
		struct face f = face_of(u->retroid);
		char hint[64];
		snprintf(hint, sizeof(hint), "%c CHANGE   %c BACK", f.bottom, f.right);
		term_puts(t, 1, t->rows - 1, hint, ATTR_MID);
	}
}

/* Saved networks first, then whatever else is in range. The distinction is
 * the point of the screen: a saved one connects on a button press, a new one
 * needs a password and there is nowhere to type it yet. */
static void draw_wifi(struct ui *u)
{
	struct term *t = &u->term;
	char buf[64];
	int on = net_wifi_enabled();

	draw_frame(u, "Settings  >  Wi-Fi");
	term_puts(t, 2, 3, on ? "NETWORKS" : "WI-FI IS OFF", ATTR_MID);
	snprintf(buf, sizeof(buf), "%d found", u->nets.n);
	term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);

	int visible = (int)list_rows(u);
	scroll_to(u->wifi_sel, &u->wifi_top, u->nets.n, visible);

	for (int i = 0; i < visible && u->wifi_top + i < u->nets.n; i++) {
		const struct net_entry *e = &u->nets.e[u->wifi_top + i];
		if (e->active)
			snprintf(buf, sizeof(buf), "connected");
		else if (e->saved)
			snprintf(buf, sizeof(buf), "saved");
		else if (e->signal >= 0)
			snprintf(buf, sizeof(buf), "%d%%", e->signal);
		else
			buf[0] = '\0';
		draw_row(u, (unsigned)(LIST_TOP + i), u->wifi_top + i == u->wifi_sel,
		         e->name, buf);
	}

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c CONNECT   %c BACK   %c RESCAN",
	         f.bottom, f.right, f.left);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Two toggles and then the devices, in one list.
 *
 * One list rather than a settings page with a sub-page, because everything
 * here is about the same four inches of the evening: switch it on, let the
 * headphones come back on their own, and if they have not, pick them.
 */
#define BT_HEAD 2          /* the two toggle rows above the device list */
#define BT_LIST_TOP 7

static int bt_rows(const struct ui *u)
{
	return (int)u->term.rows - BT_LIST_TOP - 2;
}

static void draw_bt(struct ui *u)
{
	struct term *t = &u->term;
	char buf[64];

	draw_frame(u, "Settings  >  Bluetooth");

	draw_row(u, 3, u->bt_sel == 0, "Bluetooth", u->bt_on ? "on" : "off");
	draw_row(u, 4, u->bt_sel == 1, "Auto-connect known devices",
	         u->bt_auto ? "yes" : "no");
	term_hline(t, 5, G_HLINE, ATTR_DIM);

	term_puts(t, 2, 6, "DEVICES", ATTR_MID);
	snprintf(buf, sizeof(buf), "%d found", u->bt.n);
	term_puts_right(t, t->cols - 2, 6, buf, ATTR_MID);

	int visible = bt_rows(u);
	int sel = u->bt_sel - BT_HEAD;
	scroll_to(sel < 0 ? 0 : sel, &u->bt_top, u->bt.n, visible);

	if (u->bt.n == 0)
		term_puts(t, 4, BT_LIST_TOP,
		          u->bt_on ? "none known - scan to find some"
		                   : "bluetooth is off", ATTR_DIM);

	for (int i = 0; i < visible && u->bt_top + i < u->bt.n; i++) {
		const struct bt_device *d = &u->bt.d[u->bt_top + i];
		if (d->connected)
			snprintf(buf, sizeof(buf), "connected");
		else if (d->paired)
			snprintf(buf, sizeof(buf), "%s",
			         d->trusted ? "paired" : "not trusted");
		else
			snprintf(buf, sizeof(buf), "new");
		draw_row(u, (unsigned)(BT_LIST_TOP + i), u->bt_top + i == sel,
		         d->name, buf);
	}

	struct face f = face_of(u->retroid);
	const char *verb = "CHANGE";
	if (sel >= 0 && sel < u->bt.n)
		verb = u->bt.d[sel].connected ? "DISCONNECT" : "CONNECT";
	snprintf(buf, sizeof(buf), "%c %s   %c BACK   %c SCAN",
	         f.bottom, verb, f.right, f.left);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

static void redraw(struct ui *u)
{
	switch (u->screen) {
	case SCR_SYSTEMS:  draw_systems(u);  break;
	case SCR_GAMES:    draw_games(u);    break;
	case SCR_SETTINGS: draw_settings(u); break;
	case SCR_WIFI:     draw_wifi(u);     break;
	case SCR_BT:       draw_bt(u);       break;
	}
	term_flush(&u->term);
}

/* nmcli and usbgadget both take a moment. Say so rather than appear frozen. */
static void draw_busy(struct ui *u, const char *what)
{
	struct term *t = &u->term;
	term_puts(t, 4, t->rows - 4, what, ATTR_BRIGHT);
	term_flush(t);
}

static void draw_launching(struct ui *u, const struct psystem *s,
                           const struct game *g)
{
	struct term *t = &u->term;

	draw_frame(u, "PortareOS");
	term_puts(t, 6, 6, g->name, ATTR_BRIGHT);
	term_puts(t, 6, 8, s->core[0] ? s->core : s->emulator, ATTR_MID);
	term_puts(t, 6, 10, "handing over the display...", ATTR_DIM);
	term_flush(t);
}

/* Runs the emulator with the panel, then takes it back.
 *
 * The argument list is built here rather than by substituting into the
 * <command> string from es_systems.cfg, so a game called "Ratchet & Clank"
 * stays one argument. */
static void launch(struct ui *u, const struct psystem *s, const struct game *g)
{
	char pflag[64], core[96], emu[96];

	/* An empty core reaches RetroArch as "/tmp/cores/_libretro.so" and
	 * fails with "path is not set", which looks like the launcher being
	 * broken rather than the catalog missing a value. Say which. */
	if (!s->core[0] || !s->emulator[0]) {
		struct term *t = &u->term;
		draw_frame(u, "PortareOS");
		term_puts(t, 4, 6, "Cannot launch: no", ATTR_BRIGHT);
		term_puts(t, 4, 7, s->core[0] ? "emulator" : "core", ATTR_BRIGHT);
		term_puts(t, 4, 8, "for this system.", ATTR_BRIGHT);
		term_puts(t, 4, 10, s->name, ATTR_MID);
		term_flush(t);
		return;
	}

	snprintf(pflag, sizeof(pflag), "-P%s", s->name);
	snprintf(core, sizeof(core), "--core=%s", s->core);
	snprintf(emu, sizeof(emu), "--emulator=%s", s->emulator);

	char *const argv[] = {
		(char *)(s->launcher[0] ? s->launcher : "/usr/bin/runemu.sh"),
		(char *)g->path, pflag, core, emu,
		(char *)"--controllers=", NULL
	};

	draw_launching(u, s, g);

	if (kms_drop_master(&u->kms) < 0)
		return;

	pid_t pid = fork();
	if (pid == 0) {
		execv(argv[0], argv);
		_exit(127);
	}

	if (pid < 0) {
		fprintf(stderr, "fork: %s\n", strerror(errno));
	} else {
		int status = 0;
		while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
			;
	}

	/* runemu.sh restarts the EmulationStation session on its way out,
	 * because until now something had to put a front-end back. We are the
	 * front-end and never went away, so send it back. This is a shim: the
	 * right fix is for runemu to know it was called by a resident
	 * launcher, and it belongs in that script rather than here. */
	if (system("systemctl stop essway sway >/dev/null 2>&1") < 0)
		fprintf(stderr, "could not stop the emulationstation session\n");

	kms_set_master(&u->kms);
	kms_present(&u->kms);
	term_invalidate(&u->term);   /* the emulator owned the panel; assume nothing */
}

static void on_action(struct ui *u, enum action a)
{
	const struct psystem *s = &u->cat.sys[u->sys_sel];

	if (a == ACT_AUX) {
		/* Something changed a setting we show. The pipe carries a
		 * message but the header is the display now, so only the
		 * nudge matters. */
		osd_read(&u->osd);
		status_read(&u->st);
		return;
	}

	if (a == ACT_TICK) {
		status_read(&u->st);
		return;
	}

	if (a == ACT_PALETTE) {
		u->pal_name = term_set_palette(&u->term,
		                               term_palette(&u->term) + 1);
		return;
	}

	switch (u->screen) {
	case SCR_SYSTEMS:
		if (a == ACT_UP && u->sys_sel > 0) u->sys_sel--;
		else if (a == ACT_DOWN && u->sys_sel < u->cat.n - 1) u->sys_sel++;
		else if (a == ACT_CONFIRM) {
			u->screen = SCR_GAMES;
			u->game_sel = u->game_top = 0;
		} else if (a == ACT_MENU) {
			u->screen = SCR_SETTINGS;
			u->set_sel = 0;
		} else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_GAMES:
		if (a == ACT_UP && u->game_sel > 0) u->game_sel--;
		else if (a == ACT_DOWN && u->game_sel < s->ngames - 1) u->game_sel++;
		else if (a == ACT_CONFIRM) launch(u, s, &s->games[u->game_sel]);
		else if (a == ACT_BACK) u->screen = SCR_SYSTEMS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_SETTINGS:
		if (a == ACT_UP && u->set_sel > 0) u->set_sel--;
		else if (a == ACT_DOWN && u->set_sel < N_SETTINGS - 1) u->set_sel++;
		else if (a == ACT_CONFIRM && u->set_sel == SET_WIFI) {
			draw_busy(u, "scanning...");
			net_scan(&u->nets, 1);
			u->wifi_sel = u->wifi_top = 0;
			u->screen = SCR_WIFI;
		}
		else if (a == ACT_CONFIRM && u->set_sel == SET_BLUETOOTH) {
			draw_busy(u, "reading devices...");
			u->bt_on = bt_powered();
			u->bt_auto = bt_autoconnect();
			if (u->bt_on)
				bt_list(&u->bt);
			u->bt_sel = u->bt_top = 0;
			u->screen = SCR_BT;
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_USB && u->n_usb > 0) {
			/* Cycle through whatever usbgadget --options reported,
			 * rather than a list of our own that could drift from it. */
			int cur = 0;
			for (int i = 0; i < u->n_usb; i++)
				if (strcmp(u->usb_opts[i], u->usb) == 0)
					cur = i;
			int next = (a == ACT_LEFT)
			         ? (cur + u->n_usb - 1) % u->n_usb
			         : (cur + 1) % u->n_usb;
			draw_busy(u, "switching...");
			usb_set_mode(u->usb_opts[next]);
			usb_mode(u->usb, sizeof(u->usb));
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_BUTTONS) {
			u->retroid = !u->retroid;
			/* Written straight away rather than on exit: this program
			 * can be killed from the outside and the setting that
			 * decides how to leave it should not be the one that is
			 * lost. */
			settings_set(SETTINGS, KEY_BUTTONS,
			             u->retroid ? "retroid" : "ps");
		}
		else if (a == ACT_BACK || a == ACT_MENU) u->screen = SCR_SYSTEMS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_WIFI:
		if (a == ACT_UP && u->wifi_sel > 0) u->wifi_sel--;
		else if (a == ACT_DOWN && u->wifi_sel < u->nets.n - 1) u->wifi_sel++;
		else if (a == ACT_MENU) {
			draw_busy(u, "rescanning...");
			net_scan(&u->nets, 1);
			if (u->wifi_sel >= u->nets.n) u->wifi_sel = 0;
		}
		else if (a == ACT_CONFIRM && u->wifi_sel < u->nets.n) {
			const struct net_entry *e = &u->nets.e[u->wifi_sel];
			if (!e->saved) {
				/* No password, nowhere to type one. Say which, rather
				 * than fail silently against a network we cannot join. */
				draw_busy(u, "no saved password for this network");
			} else {
				draw_busy(u, "connecting...");
				net_connect(e->name);
				net_scan(&u->nets, 0);
			}
		}
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_BT: {
		int sel = u->bt_sel - BT_HEAD;
		int last = BT_HEAD + u->bt.n - 1;

		if (a == ACT_UP && u->bt_sel > 0) u->bt_sel--;
		else if (a == ACT_DOWN && u->bt_sel < last) u->bt_sel++;
		else if (a == ACT_MENU) {
			if (!u->bt_on) {
				draw_busy(u, "bluetooth is off");
				break;
			}
			/* Eight seconds with the panel frozen. Long enough for
			 * headphones to announce themselves, short enough that
			 * nobody thinks this has crashed - which is why the
			 * message says how long. */
			draw_busy(u, "scanning for 8 seconds...");
			bt_scan(&u->bt, 8);
			if (u->bt_sel > BT_HEAD + u->bt.n - 1)
				u->bt_sel = BT_HEAD;
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->bt_sel == 0) {
			draw_busy(u, u->bt_on ? "switching off..." : "switching on...");
			bt_power(!u->bt_on);
			u->bt_on = bt_powered();
			memset(&u->bt, 0, sizeof(u->bt));
			if (u->bt_on)
				bt_list(&u->bt);
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->bt_sel == 1) {
			u->bt_auto = !u->bt_auto;
			draw_busy(u, "applying...");
			bt_set_autoconnect(u->bt_auto);
			if (u->bt_on)
				bt_list(&u->bt);
		}
		else if (a == ACT_CONFIRM && sel >= 0 && sel < u->bt.n) {
			struct bt_device d = u->bt.d[sel];
			if (d.connected) {
				draw_busy(u, "disconnecting...");
				bt_disconnect(d.addr);
			} else {
				draw_busy(u, "connecting...");
				bt_connect(d.addr);
			}
			bt_list(&u->bt);
		}
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;
	}
	}
}

int main(void)
{
	struct ui u;
	memset(&u, 0, sizeof(u));

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	/* The panel is ours, so blanking it is ours too. power-handler used to
	 * ask sway; with no compositor there is nobody else holding DRM
	 * master who could. */
	signal(SIGUSR1, on_signal);
	signal(SIGUSR2, on_signal);

	if (catalog_load(&u.cat, ES_SYSTEMS, SETTINGS) < 0)
		return 1;

	if (kms_open(&u.kms, CARD) < 0) {
		catalog_free(&u.cat);
		return 1;
	}

	if (term_init(&u.term, u.kms.map, u.kms.pitch_px,
	              u.kms.mode.hdisplay, u.kms.mode.vdisplay, SCALE) < 0) {
		fprintf(stderr, "term init failed\n");
		kms_close(&u.kms);
		catalog_free(&u.cat);
		return 1;
	}

	if (osd_open(&u.osd) == 0)
		input_set_aux(&u.in, u.osd.fd);

	if (input_open(&u.in) < 0) {
		term_free(&u.term);
		kms_close(&u.kms);
		catalog_free(&u.cat);
		return 1;
	}

	fprintf(stderr, "portarelauncher: %ux%u, %ux%u grid, %d systems\n",
	        u.kms.mode.hdisplay, u.kms.mode.vdisplay,
	        u.term.cols, u.term.rows, u.cat.n);

	/* Grey by default. Amber was tried first and read as yellow on this
	 * panel; X cycles the rest. */
	u.pal_name = term_set_palette(&u.term, 0);

	/* Default to the layout printed on this device rather than to the
	 * positional convention, which would put confirm on the button
	 * labelled B. */
	char style[32];
	/* "sony" is accepted as well as "ps" because it is what earlier builds
	 * wrote, and a setting that silently flips on upgrade is worse than a
	 * spare string comparison. */
	u.retroid = !(settings_get(SETTINGS, KEY_BUTTONS, style, sizeof(style)) &&
	              (strcmp(style, "ps") == 0 || strcmp(style, "sony") == 0));
	/* Read what is cheap now and leave the scan until the Wi-Fi screen is
	 * opened: a rescan takes seconds and nothing on the first screen shows
	 * it. */
	usb_modes(u.usb_opts, &u.n_usb, 8);
	usb_mode(u.usb, sizeof(u.usb));
	net_scan(&u.nets, 0);
	u.bt_auto = bt_autoconnect();
	u.bt_on = bt_powered();
	if (u.bt_on)
		bt_list(&u.bt);

	u.screen = SCR_SYSTEMS;
	u.running = 1;
	status_read(&u.st);
	redraw(&u);

	while (u.running && !stop_requested) {
		/* Sleep until the minute turns over, until the overlay is due
		 * to come down, or until something is pressed. Nothing else
		 * wakes this program. */
		int idle = status_ms_to_next_minute();
		int osd_left = osd_remaining(&u.osd);
		if (osd_left >= 0 && osd_left < idle)
			idle = osd_left;

		enum action a = input_wait(&u.in, idle);

		if (blank_requested) {
			int on = blank_requested > 0;
			blank_requested = 0;
			if (on) {
				kms_present(&u.kms);
				term_invalidate(&u.term);
				status_read(&u.st);
				redraw(&u);
			} else {
				kms_blank(&u.kms);
			}
			continue;
		}

		if (a == ACT_NONE)
			continue;
		on_action(&u, a);
		osd_remaining(&u.osd);   /* clears the text once it has expired */
		redraw(&u);
	}

	osd_close(&u.osd);
	input_close(&u.in);
	term_free(&u.term);
	kms_close(&u.kms);
	catalog_free(&u.cat);
	return 0;
}
