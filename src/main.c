/* portarelauncher - a KMS launcher for PortareOS.
 *
 * Owns the panel, lists what there is to play, hands the display to an
 * emulator and takes it back. See README.md for why it looks like this.
 */
#include "bt.h"
#include "catalog.h"
#include "color.h"
#include "input.h"
#include "kms.h"
#include "net.h"
#include "osd.h"
#include "osinfo.h"
#include "osk.h"
#include "proc.h"
#include "quit.h"
#include "settings.h"
#include "status.h"
#include "term.h"
#include "tools.h"
#include "tz.h"
#include "update.h"

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CARD        "/dev/dri/card0"
#define ES_SYSTEMS  "/usr/config/emulationstation/es_systems.cfg"
#define SETTINGS    "/storage/.config/system/configs/system.cfg"

/* The panel's color profile. "stock" leaves the display controller's color
 * blocks off; the others load a measured correction shipped with the image,
 * /usr/config/color/<key>.profile. The key is the setting's value and the
 * file's name, so a new profile is a new file and a row here. */
#define KEY_PROFILE "display.colorprofile"
/* Yellow thumbsticks while the battery charges; a daemon on the image
 * reads this every few seconds. */
#define KEY_CHARGING "led.charging"
#define PROFILE_DIR "/usr/config/color"
static const struct { const char *key, *label; } profile_names[] = {
	{ "stock",   "stock" },
	{ "gamma22", "Gamma 2.2" },
	{ "srgb",    "sRGB" },
};
#define N_PROFILES ((int)(sizeof(profile_names) / sizeof(profile_names[0])))

#ifndef PL_VERSION
#define PL_VERSION "dev"   /* set by the Makefile, from VERSION or git */
#endif
#define SCALE       3          /* 8x16 glyphs at 3x is 53x20 on this panel */

#define LIST_TOP    5
#define LIST_MARGIN 3          /* rows kept below the list for the footer  */

enum screen { SCR_SYSTEMS, SCR_GAMES, SCR_SETTINGS, SCR_WIFI, SCR_BT,
              SCR_KEYBOARD, SCR_TOOLS, SCR_ABOUT, SCR_UPDATE,
              SCR_TZ, SCR_POWER };

struct ui {
	struct term term;
	struct kms kms;
	struct input in;
	struct catalog cat;

	const char *pal_name;
	struct color_profile profiles[N_PROFILES];
	int profile_ok[N_PROFILES];  /* the file loaded; [0], stock, always */
	int profile;         /* the one applied, an index into profiles[]     */
	int charging_led;    /* led.charging, 1 unless the file says 0        */
	struct status st;
	struct osd osd;

	struct net_list nets;
	int wifi_on;         /* the radio, as last asked                      */
	int wifi_sel, wifi_top;  /* row 0 is the switch, 1.. the networks     */
	int ssh_on;          /* sshd, read when Settings opens                */

	struct bt_list bt;
	int bt_on, bt_auto;
	int bt_sel, bt_top;

	struct osk osk;
	char join_ssid[80];

	struct tools tools;
	int tool_sel, tool_top;

	struct tzlist tz;
	int tz_idx[TZ_MAX];  /* the zones of the region being shown        */
	int tz_nidx;
	int tz_level;        /* 0 the regions, 1 the cities of one           */
	int tz_region;
	int tz_sel, tz_top;
	char tz_cur[48];     /* the zone in use                              */

	int power_sel;
	int power_armed;     /* the row pressed once, waiting for a second; -1 */
	int going_down;      /* reboot or poweroff accepted, panel off         */
	int panel_lost;      /* master not yet back after a child; retrying    */

	struct osinfo os;
	char addr[40];       /* empty when offline */
	long long net_next;  /* when to look at the address again, ms      */
	long long started;   /* ms, for the quick polling after a boot     */
	int about_sel;

	struct update_info upd;
	int upd_sel;
	int upd_staged;      /* a verified .tar is waiting for the next boot */
	/* One line of news for the screen that is up, kept until the next
	 * press. Drawn by the screen itself, because anything drawn beside
	 * the screen is gone in the redraw that follows every action. */
	char note[64];
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
#define KEY_PALETTE "launcher.palette"


/* The PS marks approximated out of CP437, which is all the VGA font has.
 * Close enough to be recognised, and not the real symbols. */
#define G_CIRCLE    0x09   /* O   */
#define G_TRIANGLE  0x1E   /* /\  */
#define G_SQUARE    0xFE   /* []  */

enum { SET_WIFI = 0, SET_SSH, SET_BLUETOOTH, SET_USB, SET_BUTTONS, SET_COLOR,
       SET_PROFILE, SET_CHARGING, SET_TIMEZONE, SET_ABOUT, SET_POWER, N_SETTINGS };

static const char *const settings_labels[N_SETTINGS] = {
	"Wi-Fi",
	"SSH",
	"Bluetooth",
	"USB gadget mode",
	"Button style",
	"Color",
	"Color profile",
	"Charging LED",
	"Time zone",
	"About",
	"Power",
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

	/* The status is 32 columns at typical values and 35 with everything
	 * at 100%, which reaches back past "Settings  >  Bluetooth". It is the
	 * part that changes and has to stay legible, so the crumb yields: cut
	 * to end two columns short of it. */
	int room = (int)t->cols - 1 - (int)strlen(right) - 2 - 1;
	char c[64];
	if (room > (int)sizeof(c) - 1)
		room = (int)sizeof(c) - 1;
	if (room > 0) {
		snprintf(c, (size_t)room + 1, "%s", crumb);
		term_puts(t, 1, 0, c, ATTR_TEXT);
	}

	term_hline(t, 1, G_HLINE_D, ATTR_DIM);
	term_hline(t, t->rows - 2, G_HLINE, ATTR_DIM);
}

/* What the four face buttons are called on the pad in the user's hands,
 * by position, and which of them does what: the hints name the button by
 * its role, the diagram under the setting shows where it sits. */
struct face {
	unsigned char bottom, right, top, left;
	unsigned char confirm, back, menu;
};

static struct face face_of(int retroid)
{
	struct face f;
	if (retroid) {
		/* What the printing says: A confirms. */
		f.bottom = 'B'; f.right = 'A'; f.top = 'X'; f.left = 'Y';
		f.confirm = f.right; f.back = f.bottom;
		f.menu = f.top;
	} else {
		f.bottom = 'X'; f.right = G_CIRCLE;
		f.top = G_TRIANGLE; f.left = G_SQUARE;
		f.confirm = f.bottom; f.back = f.right;
		f.menu = f.top;
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

/* The systems, then Tools as one more row - where EmulationStation kept it,
 * and where anyone coming from it will look. Only there when the folder
 * has something in it. */
static int system_rows(const struct ui *u)
{
	return u->cat.n + (u->tools.n > 0);
}

static int on_tools_row(const struct ui *u)
{
	return u->tools.n > 0 && u->sys_sel == u->cat.n;
}

static void draw_systems(struct ui *u)
{
	struct term *t = &u->term;
	char buf[64];

	draw_frame(u, "PortareOS");

	snprintf(buf, sizeof(buf), "%d found", u->cat.n);
	term_puts(t, 2, 3, "SYSTEMS", ATTR_MID);
	term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);

	int rows = system_rows(u);
	int visible = (int)list_rows(u);
	scroll_to(u->sys_sel, &u->sys_top, rows, visible);

	for (int i = 0; i < visible && u->sys_top + i < rows; i++) {
		int idx = u->sys_top + i;
		if (idx == u->cat.n) {
			snprintf(buf, sizeof(buf), "%d", u->tools.n);
			draw_row(u, (unsigned)(LIST_TOP + i), idx == u->sys_sel, "Tools", buf);
			continue;
		}
		const struct psystem *s = &u->cat.sys[idx];
		snprintf(buf, sizeof(buf), "%d", s->ngames);
		draw_row(u, (unsigned)(LIST_TOP + i), idx == u->sys_sel,
		         s->fullname[0] ? s->fullname : s->name, buf);
	}

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c SELECT   %c SETTINGS", f.confirm, f.menu);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
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
	snprintf(buf, sizeof(buf), "%c LAUNCH   %c BACK", f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* A diamond of the four face buttons, because the argument is about where
 * they are rather than what they are called. */
static void draw_face(struct ui *u, unsigned y, int retroid)
{
	struct term *t = &u->term;
	struct face f = face_of(retroid);

	/* The button that confirms is the bright one, wherever it sits: on
	 * the right of a Retroid, at the bottom of a PS pad. */
#define FACE_ATTR(g) ((g) == f.confirm ? ATTR_BRIGHT : ATTR_TEXT)
	term_putc(t, 9,  y,     f.top,    FACE_ATTR(f.top));
	term_putc(t, 6,  y + 1, f.left,   FACE_ATTR(f.left));
	term_putc(t, 12, y + 1, f.right,  FACE_ATTR(f.right));
	term_putc(t, 9,  y + 2, f.bottom, FACE_ATTR(f.bottom));
#undef FACE_ATTR

	term_putc(t, 20, y,     f.confirm, ATTR_BRIGHT);
	term_puts(t, 22, y,     "confirm", ATTR_MID);
	term_putc(t, 20, y + 1, f.back,    ATTR_TEXT);
	term_puts(t, 22, y + 1, "back", ATTR_MID);
	term_putc(t, 20, y + 2, f.menu,    ATTR_TEXT);
	term_puts(t, 22, y + 2, "settings", ATTR_MID);
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
			/* The list is from the last scan; the address is live. */
			value = ssid ? ssid : u->addr[0] ? "connected"
			      : (net_wifi_enabled() ? "not connected" : "off");
			break;
		}
		case SET_SSH:
			value = u->ssh_on ? "on" : "off";
			break;
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
		case SET_COLOR:
			value = u->pal_name;
			break;
		case SET_PROFILE:
			value = profile_names[u->profile].label;
			break;
		case SET_CHARGING:
			value = u->charging_led ? "on" : "off";
			break;
		case SET_POWER:
			value = "";
			break;
		case SET_TIMEZONE:
			if (settings_get(SETTINGS, TZ_KEY, val, sizeof(val)))
				value = val;
			else
				value = "UTC";
			break;
		case SET_ABOUT:
			if (u->upd_staged)
				value = "restart to install";
			else if (u->os.version[0]) {
				snprintf(val, sizeof(val), "%s %s", u->os.version, u->os.build);
				value = val;
			} else
				value = "unknown";
			break;
		}
		draw_row(u, (unsigned)(3 + i), i == u->set_sel,
		         settings_labels[i], value);
	}

	/* Eleven rows of settings leave three under the rule before the
	 * bottom rule and the hint line: y + 1 to y + 3, and the button
	 * diagram needs all of them. */
	unsigned y = 3 + N_SETTINGS;
	term_hline(t, y, G_HLINE, ATTR_DIM);

	switch (u->set_sel) {
	case SET_BUTTONS:
		draw_face(u, y + 1, u->retroid);
		break;
	case SET_WIFI: {
		char addr[40] = "";
		net_address(addr, sizeof(addr));
		if (addr[0]) {
			snprintf(val, sizeof(val), "address  %s", addr);
			term_puts(t, 4, y + 1, val, ATTR_MID);
		}
		break;
	}
	case SET_SSH: {
		char addr[40] = "";
		net_address(addr, sizeof(addr));
		if (u->ssh_on && addr[0]) {
			snprintf(val, sizeof(val), "ssh root@%s", addr);
			term_puts(t, 4, y + 1, val, ATTR_MID);
		}
		break;
	}
	case SET_COLOR:
		/* The whole screen is already the preview. */
		break;
	case SET_PROFILE:
		/* The correction is in the display controller, ahead of the
		 * panel, so it holds for everything drawn after this: games,
		 * films, the launcher itself. */
		term_puts(t, 4, y + 1, "sRGB, D65; Gamma 2.2 for consoles.", ATTR_DIM);
		if (!u->profile_ok[1] && !u->profile_ok[2])
			term_puts(t, 4, y + 1, "no profile files on this image", ATTR_MID);
		break;
	case SET_CHARGING:
		term_puts(t, 4, y + 1, "Yellow thumbsticks while charging.", ATTR_DIM);
		break;
	case SET_BLUETOOTH:
		term_puts(t, 4, y + 1, "Open to scan and connect.", ATTR_DIM);
		break;
	case SET_TIMEZONE:
		term_puts(t, 4, y + 1, "Open to pick a region, then a city.", ATTR_DIM);
		break;
	case SET_POWER:
		term_puts(t, 4, y + 1, "Restart, or switch the device off.", ATTR_DIM);
		break;
	case SET_ABOUT:
		term_puts(t, 4, y + 1, "Version, address, updates.", ATTR_DIM);
		break;
	case SET_USB: {
		char addr[40] = "";
		usb_address(addr, sizeof(addr));
		term_puts(t, 4, y + 1, "USB as a network link, or as file transfer.", ATTR_DIM);
		if (addr[0] && strcmp(u->usb, "network") == 0) {
			snprintf(val, sizeof(val), "address  %s", addr);
			term_puts(t, 4, y + 2, val, ATTR_MID);
		}
		break;
	}
	default:
		term_puts(t, 4, y + 1, "Not implemented yet.", ATTR_DIM);
		break;
	}

	{
		struct face f = face_of(u->retroid);
		char hint[64];
		snprintf(hint, sizeof(hint), "%c CHANGE   %c BACK", f.confirm, f.back);
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

	draw_frame(u, "Settings  >  Wi-Fi");

	/* The switch first. An official image boots with Wi-Fi off, and until
	 * this row there was no way to turn it on from the device - no network
	 * to join, and no update. */
	draw_row(u, 3, u->wifi_sel == 0, "Wi-Fi", u->wifi_on ? "on" : "off");

	if (!u->wifi_on) {
		term_puts(t, 4, LIST_TOP + 1, "Switch Wi-Fi on to see the networks", ATTR_DIM);
		term_puts(t, 4, LIST_TOP + 2, "in range and join one.", ATTR_DIM);
	} else {
		term_puts(t, 2, LIST_TOP - 1, "NETWORKS", ATTR_MID);
		snprintf(buf, sizeof(buf), "%d found", u->nets.n);
		term_puts_right(t, t->cols - 2, LIST_TOP - 1, buf, ATTR_MID);
	}

	int visible = (int)list_rows(u);
	int net_sel = u->wifi_sel - 1;          /* -1 while the switch is selected */
	scroll_to(net_sel < 0 ? 0 : net_sel, &u->wifi_top, u->nets.n, visible);

	for (int i = 0; u->wifi_on && i < visible && u->wifi_top + i < u->nets.n; i++) {
		const struct net_entry *e = &u->nets.e[u->wifi_top + i];
		if (e->active)
			snprintf(buf, sizeof(buf), "connected");
		else if (e->saved)
			snprintf(buf, sizeof(buf), "saved");
		else if (e->signal >= 0)
			snprintf(buf, sizeof(buf), "%d%%", e->signal);
		else
			buf[0] = '\0';
		draw_row(u, (unsigned)(LIST_TOP + i), u->wifi_top + i == net_sel,
		         e->name, buf);
	}

	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	struct face f = face_of(u->retroid);
	if (u->wifi_sel == 0)
		snprintf(buf, sizeof(buf), "%c SWITCH   %c BACK", f.confirm, f.back);
	else
		snprintf(buf, sizeof(buf), "%c CONNECT   %c BACK   %c RESCAN",
		         f.confirm, f.back, f.menu);
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
	         f.confirm, verb, f.back, f.menu);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Writes text into a box of `width` columns and up to `lines` rows,
 * breaking at spaces. Returns the rows used. */
static unsigned wrap_puts(struct term *t, unsigned x, unsigned y, unsigned width,
                          unsigned lines, const char *text, int attr)
{
	unsigned row = 0;
	char buf[128];
	while (*text && row < lines) {
		while (*text == ' ')
			text++;
		size_t len = strlen(text);
		size_t take = len < width ? len : width;
		if (take < len) {
			size_t sp = take;
			while (sp > 0 && text[sp] != ' ')
				sp--;
			if (sp > 0)
				take = sp;             /* else one long word: hard break */
		}
		if (take >= sizeof(buf))
			take = sizeof(buf) - 1;
		memcpy(buf, text, take);
		buf[take] = '\0';
		term_puts(t, x, y + row++, buf, attr);
		text += take;
	}
	return row;
}

#define TOOLS_DESC_LINES 4

static void draw_tools(struct ui *u)
{
	struct term *t = &u->term;
	char buf[64];

	draw_frame(u, "Tools");
	snprintf(buf, sizeof(buf), "%d found", u->tools.n);
	term_puts(t, 2, 3, "TOOLS", ATTR_MID);
	term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);

	/* The description is what says how to get back out of a tool, so it
	 * gets fixed room below the list rather than whatever is left. */
	unsigned desc_y = t->rows - 2 - TOOLS_DESC_LINES;
	int visible = (int)(desc_y - 1 - LIST_TOP);
	scroll_to(u->tool_sel, &u->tool_top, u->tools.n, visible);

	for (int i = 0; i < visible && u->tool_top + i < u->tools.n; i++)
		draw_row(u, (unsigned)(LIST_TOP + i), u->tool_top + i == u->tool_sel,
		         u->tools.t[u->tool_top + i].name, NULL);

	term_hline(t, desc_y - 1, G_HLINE, ATTR_DIM);
	if (u->tool_sel < u->tools.n)
		wrap_puts(t, 4, desc_y, t->cols - 8, TOOLS_DESC_LINES,
		          u->tools.t[u->tool_sel].desc, ATTR_TEXT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c RUN   %c BACK", f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* The password for a network that has never been joined. The layout is in
 * docs/mockup.txt; the keyboard itself is osk.c. What this adds is the
 * frame, the count, and the one line that says what went wrong. */
static void draw_keyboard(struct ui *u)
{
	struct term *t = &u->term;
	char buf[80];

	draw_frame(u, "Settings  >  Wi-Fi");
	term_puts(t, 2, 2, "NETWORK", ATTR_MID);
	term_puts(t, 11, 2, u->join_ssid, ATTR_TEXT);

	term_puts(t, 2, 3, "PASSWORD", ATTR_MID);
	if (u->osk.len < u->osk.min_len)
		snprintf(buf, sizeof(buf), "%d / %d  at least %d", u->osk.len,
		         OSK_MAX, u->osk.min_len);
	else
		snprintf(buf, sizeof(buf), "%d / %d", u->osk.len, OSK_MAX);
	term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);

	unsigned y = osk_draw(&u->osk, t, 4);
	if (u->note[0])
		term_puts(t, 4, y + 1, u->note, ATTR_BRIGHT);
	else
		term_puts(t, 4, y + 1, "SELECT shows or hides the password", ATTR_DIM);

	/* Built from the pad's own printing, like every other hint line, so
	 * it names the buttons the user is actually holding. */
	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c TYPE  %c DELETE  %c SPACE  L1 SHIFT  START JOIN",
	         f.confirm, f.back, f.menu);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* ---- power -------------------------------------------------------------- */

/* The panel goes off first, so the last thing on it is not a menu that has
 * stopped answering while systemd takes everything down. If systemctl
 * refuses, the panel comes back with the reason; if it accepts and the
 * system still has not gone down by the next press, that press brings the
 * panel back rather than leaving a dark screen with no way out. */
static void power(struct ui *u, const char *verb)
{
	kms_blank(&u->kms);
	char *const argv[] = { (char *)"/usr/bin/systemctl", (char *)verb, NULL };
	int rc = proc_run_for(argv, NULL, NULL, 10000);
	if (rc == 0) {
		u->going_down = 1;
		return;
	}
	kms_present(&u->kms);
	term_invalidate(&u->term);
	snprintf(u->note, sizeof(u->note), "Could not %s (%d).",
	         strcmp(verb, "reboot") == 0 ? "restart" : "switch off", rc);
}

static const char *const power_rows[] = { "Restart", "Power off" };
static const char *const power_verbs[] = { "reboot", "poweroff" };

static void draw_power(struct ui *u)
{
	struct term *t = &u->term;
	struct face f = face_of(u->retroid);
	char buf[64];

	draw_frame(u, "Settings  >  Power");
	for (int i = 0; i < 2; i++)
		draw_row(u, (unsigned)(3 + i), i == u->power_sel, power_rows[i], NULL);
	term_hline(t, 6, G_HLINE, ATTR_DIM);

	/* One press arms it and says so; the second does it. Anything else
	 * disarms, so a stray press on the way through never switches off. */
	if (u->power_armed >= 0) {
		snprintf(buf, sizeof(buf), "Press %c again to %s.", f.confirm,
		         u->power_armed == 0 ? "restart" : "switch off");
		term_puts(t, 4, 8, buf, ATTR_BRIGHT);
	}
	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	snprintf(buf, sizeof(buf), "%c SELECT   %c BACK", f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* ---- time zone ------------------------------------------------------------ */

/* A region, then its cities with the time it is there now: the quickest way
 * to find the right one is often to look for the clock that is right. */
static void draw_tz(struct ui *u)
{
	struct term *t = &u->term;
	char buf[96], now[8];
	int visible = (int)list_rows(u);

	if (u->tz_level == 0) {
		draw_frame(u, "Settings  >  Time zone");
		term_puts(t, 2, 3, "REGION", ATTR_MID);
		term_puts_right(t, t->cols - 2, 3, u->tz_cur, ATTR_MID);
		scroll_to(u->tz_sel, &u->tz_top, u->tz.nregions, visible);
		for (int i = 0; i < visible && u->tz_top + i < u->tz.nregions; i++) {
			const char *r = u->tz.region[u->tz_top + i];
			size_t n = strlen(r);
			int here = strncmp(u->tz_cur, r, n) == 0 &&
			           (u->tz_cur[n] == '/' || u->tz_cur[n] == '\0');
			draw_row(u, (unsigned)(LIST_TOP + i), u->tz_top + i == u->tz_sel,
			         r, here ? "current" : NULL);
		}
	} else {
		snprintf(buf, sizeof(buf), "Settings  >  Time zone  >  %s",
		         u->tz.region[u->tz_region]);
		draw_frame(u, buf);
		term_puts(t, 2, 3, "CITY", ATTR_MID);
		snprintf(buf, sizeof(buf), "%d", u->tz_nidx);
		term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);
		scroll_to(u->tz_sel, &u->tz_top, u->tz_nidx, visible);
		for (int i = 0; i < visible && u->tz_top + i < u->tz_nidx; i++) {
			const char *zone = u->tz.zone[u->tz_idx[u->tz_top + i]];
			char city[48];
			tz_city(zone, city, sizeof(city));
			tz_clock(zone, now, sizeof(now));
			snprintf(buf, sizeof(buf), "%s%s", now,
			         strcmp(zone, u->tz_cur) == 0 ? "  current" : "");
			draw_row(u, (unsigned)(LIST_TOP + i), u->tz_top + i == u->tz_sel,
			         city, buf);
		}
	}

	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c %s   %c BACK", f.confirm,
	         u->tz_level ? "SET" : "OPEN", f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* The cities of one region, with the zone in use selected if it is there. */
static void tz_show_region(struct ui *u, int region)
{
	u->tz_region = region;
	u->tz_nidx = tz_in_region(&u->tz, u->tz.region[region], u->tz_idx, TZ_MAX);
	u->tz_sel = u->tz_top = 0;
	for (int i = 0; i < u->tz_nidx; i++)
		if (strcmp(u->tz.zone[u->tz_idx[i]], u->tz_cur) == 0)
			u->tz_sel = i;
	u->tz_level = 1;
}

/* Read when opened, not at start: 600 lines nobody needs until now. */
static void open_tz(struct ui *u)
{
	tz_load(&u->tz, TZ_LIST, TZ_ZONEINFO);
	if (!settings_get(SETTINGS, TZ_KEY, u->tz_cur, sizeof(u->tz_cur)))
		str_copy(u->tz_cur, sizeof(u->tz_cur), "UTC");
	u->tz_level = 0;
	u->tz_sel = u->tz_top = 0;
	for (int i = 0; i < u->tz.nregions; i++) {
		size_t n = strlen(u->tz.region[i]);
		if (strncmp(u->tz_cur, u->tz.region[i], n) == 0 &&
		    (u->tz_cur[n] == '/' || u->tz_cur[n] == '\0'))
			u->tz_sel = i;
	}
	u->screen = SCR_TZ;
}

/* ---- update --------------------------------------------------------------- */

static void draw_busy(struct ui *u, const char *what);

#define STAGE_DIR "/storage/.update"

static const char *update_action(const struct ui *u)
{
	if (u->upd_staged)
		return "Restart to install";
	if (u->upd.state == UPD_AVAILABLE)
		return "Download and install";
	return "Check again";
}

/* What is installed and where it can be reached, then Update. The version is
 * what a bug report needs first; the address and password are what ssh
 * needs. None of it was anywhere on the device before. */
static void draw_about(struct ui *u)
{
	struct term *t = &u->term;
	char buf[96], val[16];
	const struct osinfo *o = &u->os;

	draw_frame(u, "Settings  >  About");

	const char *upd;
	if (u->upd_staged)
		upd = "restart to install";
	else if (settings_get(SETTINGS, "updates.branch", val, sizeof(val)) &&
	         (!strcmp(val, "nightly") || !strcmp(val, "release")))
		upd = val;
	else
		upd = "automatic";
	draw_row(u, 3, u->about_sel == 0, "Update", upd);
	term_hline(t, 5, G_HLINE, ATTR_DIM);

	term_puts(t, 4, 7, "version", ATTR_MID);
	snprintf(buf, sizeof(buf), "%s  %s", o->version[0] ? o->version : "unknown",
	         o->build);
	term_puts(t, 15, 7, buf, ATTR_TEXT);

	term_puts(t, 4, 8, "commit", ATTR_MID);
	snprintf(buf, sizeof(buf), "%.7s  %s", o->commit[0] ? o->commit : "-",
	         o->branch);
	term_puts(t, 15, 8, buf, ATTR_TEXT);

	term_puts(t, 4, 9, "built", ATTR_MID);
	term_puts(t, 15, 9, o->date[0] ? o->date : "-", ATTR_TEXT);

	term_puts(t, 4, 10, "device", ATTR_MID);
	snprintf(buf, sizeof(buf), "%s  %s", o->device, o->cpu);
	term_puts(t, 15, 10, buf, ATTR_TEXT);

	term_puts(t, 4, 11, "address", ATTR_MID);
	term_puts(t, 15, 11, u->addr[0] ? u->addr : "offline", ATTR_TEXT);

	/* The root password, which ssh asks for. Every device makes its own
	 * on first boot (portareos 007-rootpw), so this is the only place to
	 * learn it without already being logged in. */
	char pw[40];
	term_puts(t, 4, 12, "password", ATTR_MID);
	term_puts(t, 15, 12,
	          settings_get(SETTINGS, "root.password", pw, sizeof(pw)) ? pw : "-",
	          ATTR_TEXT);

	term_puts(t, 4, 13, "launcher", ATTR_MID);
	term_puts(t, 15, 13, PL_VERSION, ATTR_TEXT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c OPEN   %c BACK", f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Two rows - the channel and whatever the next step is - and what is known
 * underneath. One action row that changes rather than three that are mostly
 * greyed out: at any moment there is exactly one thing to do next. */
static void draw_update(struct ui *u)
{
	struct term *t = &u->term;
	char buf[128];

	draw_frame(u, "Settings  >  About  >  Update");
	draw_row(u, 3, u->upd_sel == 0, "Channel",
	         u->upd.channel[0] ? u->upd.channel : "unknown");
	draw_row(u, 4, u->upd_sel == 1, update_action(u), NULL);
	term_hline(t, 6, G_HLINE, ATTR_DIM);

	term_puts(t, 4, 8, "installed", ATTR_MID);
	term_puts(t, 16, 8, u->upd.installed[0] ? u->upd.installed : "-", ATTR_TEXT);
	if (u->upd.tag[0]) {
		term_puts(t, 4, 9, "latest", ATTR_MID);
		snprintf(buf, sizeof(buf), "%s  %lld MB", u->upd.tag,
		         u->upd.size / (1024 * 1024));
		term_puts(t, 16, 9, buf, ATTR_TEXT);
	}

	const char *l1 = "", *l2 = "";
	if (u->upd_staged) {
		l1 = "Downloaded and checked. Restart to";
		l2 = "install it; that takes a minute.";
	} else switch (u->upd.state) {
	case UPD_AVAILABLE: l1 = "An update is available."; break;
	case UPD_CURRENT:   l1 = "This is the newest build on this"; l2 = "channel."; break;
	case UPD_NEWER:     l1 = "The installed build is newer than"; l2 = "anything on this channel."; break;
	case UPD_NONE:      l1 = "Nothing published on this channel"; l2 = "for this device yet."; break;
	case UPD_ERROR:     l1 = u->upd.error; break;
	case UPD_UNKNOWN:   break;
	}
	term_puts(t, 4, 11, l1, ATTR_BRIGHT);
	term_puts(t, 4, 12, l2, ATTR_BRIGHT);

	if (!strcmp(u->upd.channel, "release")) {
		term_puts(t, 4, 14, "release: the monthly builds only.", ATTR_DIM);
		term_puts(t, 4, 15, "Fewer changes, each one tested longer.", ATTR_DIM);
	} else {
		term_puts(t, 4, 14, "nightly: every build, most days.", ATTR_DIM);
		term_puts(t, 4, 15, "Newest fixes first, and newest bugs.", ATTR_DIM);
	}

	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), "%c SELECT   %c BACK", f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Called by update_fetch as the download moves. Draws the whole screen each
 * time, which is once per percent - a hundred frames over several minutes. */
static void draw_download(int pct, int verifying, void *ctx)
{
	struct ui *u = ctx;
	struct term *t = &u->term;
	char buf[64];

	draw_frame(u, "Settings  >  About  >  Update");
	term_puts(t, 4, 4, verifying ? "Checking the download..." : "Downloading",
	          ATTR_BRIGHT);
	term_puts(t, 4, 5, u->upd.tag, ATTR_MID);

	unsigned width = t->cols - 8 - 6;
	unsigned filled = (unsigned)pct * width / 100;
	for (unsigned i = 0; i < width; i++)
		term_putc(t, 4 + i, 8, i < filled ? 0xDB : 0xB0,
		          i < filled ? ATTR_BRIGHT : ATTR_DIM);
	snprintf(buf, sizeof(buf), "%3d%%", pct);
	term_puts(t, 4 + width + 1, 8, buf, ATTR_TEXT);

	if (u->upd.size > 0) {
		long long mb = u->upd.size / (1024 * 1024);
		snprintf(buf, sizeof(buf), "%lld of %lld MB", mb * pct / 100, mb);
		term_puts(t, 4, 10, buf, ATTR_MID);
	}

	term_puts(t, 4, 13, "Keep Wi-Fi on. If it stops, choosing", ATTR_DIM);
	term_puts(t, 4, 14, "it again continues where it was.", ATTR_DIM);
	term_flush(t);
}

/* The address is read when the screen opens rather than kept fresh: it
 * changes with the network, and nobody is looking at it the rest of the
 * time. */
static void open_about(struct ui *u)
{
	net_address(u->addr, sizeof(u->addr));
	u->upd_staged = update_staged(STAGE_DIR);
	u->about_sel = 0;
	u->screen = SCR_ABOUT;
}

static void open_update(struct ui *u)
{
	draw_busy(u, "checking for updates...");
	update_check(&u->upd);
	u->upd_staged = update_staged(STAGE_DIR);
	u->upd_sel = 1;
	u->screen = SCR_UPDATE;
}

/* The update is applied by the boot that follows. */
static void restart(struct ui *u)
{
	power(u, "reboot");
}

static void redraw(struct ui *u)
{
	switch (u->screen) {
	case SCR_SYSTEMS:  draw_systems(u);  break;
	case SCR_GAMES:    draw_games(u);    break;
	case SCR_SETTINGS: draw_settings(u); break;
	case SCR_WIFI:     draw_wifi(u);     break;
	case SCR_BT:       draw_bt(u);       break;
	case SCR_KEYBOARD: draw_keyboard(u); break;
	case SCR_TOOLS:    draw_tools(u);    break;
	case SCR_ABOUT:    draw_about(u);    break;
	case SCR_TZ:       draw_tz(u);       break;
	case SCR_POWER:    draw_power(u);    break;
	case SCR_UPDATE:   draw_update(u);   break;
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

static void draw_launching(struct ui *u, const char *what, const char *detail)
{
	struct term *t = &u->term;

	draw_frame(u, "PortareOS");
	term_puts(t, 6, 6, what, ATTR_BRIGHT);
	term_puts(t, 6, 8, detail, ATTR_MID);
	term_puts(t, 6, 10, "handing over the display...", ATTR_DIM);
	term_flush(t);
}

/* Gives the panel to argv and takes it back when argv exits: drop DRM
 * master, run, wait, take it again and repaint everything. The same for
 * an emulator and for a tool, because both are a program that wants the
 * whole display and then gives it back.
 *
 * While it runs, Home + START is the way out of it, whatever it is - see
 * quit.h. The child gets a process group of its own so its descendants can
 * be found; the wait is on a pidfd, so the launcher sleeps until the child
 * exits or one of the two buttons changes, and on nothing else. */
#define QUIT_GRACE_MS 1500    /* for a program that quits on the combo itself */
#define QUIT_TERM_MS  5000    /* between SIGTERM and SIGKILL                  */

/* The network comes up on its own time after a boot: the radio is unblocked,
 * the firmware loads, iwd scans, NetworkManager joins. Nothing tells this
 * program when. So the address is looked at every few seconds while there
 * is none, and once a minute after that, from the kernel's interface list -
 * no process, no wait - and the screen that shows it is repainted when it
 * changes. */
#define NET_POLL_FAST_MS   3000
#define NET_POLL_SLOW_MS  60000
#define NET_POLL_FAST_FOR 300000   /* 5 minutes of quick looks after start */

static long long now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void net_poll(struct ui *u)
{
	long long now = now_ms();
	if (now < u->net_next)
		return;
	char addr[40];
	net_address(addr, sizeof(addr));
	if (strcmp(addr, u->addr) != 0) {
		snprintf(u->addr, sizeof(u->addr), "%s", addr);
		if (u->screen == SCR_SETTINGS || u->screen == SCR_ABOUT)
			redraw(u);
	}
	int quick = !u->addr[0] && now - u->started < NET_POLL_FAST_FOR;
	u->net_next = now + (quick ? NET_POLL_FAST_MS : NET_POLL_SLOW_MS);
}

static int open_pidfd(pid_t pid)
{
#ifdef SYS_pidfd_open
	return (int)syscall(SYS_pidfd_open, pid, 0);
#else
	(void)pid;
	return -1;
#endif
}

static void wait_or_quit(struct ui *u, pid_t pid)
{
	enum { RUNNING, GRACE, TERMED, KILLED } stage = RUNNING;
	long long deadline = 0;
	struct quit_combo q;
	quit_reset(&q);

	int pidfd = open_pidfd(pid);
	input_quit_only(&u->in, 1);

	for (;;) {
		int status;
		pid_t r = waitpid(pid, &status, WNOHANG);
		if (r == pid || (r < 0 && errno != EINTR))
			break;

		struct pollfd pfd[INPUT_MAX_DEV + 2];
		int ndev = input_fill_poll(&u->in, pfd);
		int n = ndev;
		if (pidfd >= 0)
			pfd[n++] = (struct pollfd){ .fd = pidfd, .events = POLLIN };

		/* Without a pidfd there is nothing to wake on when the child
		 * exits, so look every half second instead. */
		int timeout = pidfd >= 0 ? -1 : 500;
		if (stage != RUNNING) {
			long long left = deadline - now_ms();
			int l = left > 0 ? (int)left : 0;
			if (timeout < 0 || l < timeout)
				timeout = l;
		}

		if (poll(pfd, (nfds_t)n, timeout) < 0 && errno != EINTR)
			break;

		/* InputPlumber restarting mid-game replaces the pad: follow it,
		 * or the combo is read from a device that no longer exists. The
		 * slots move with the reopen, so start the combo afresh. */
		if (input_refresh(&u->in, pfd, ndev)) {
			quit_reset(&q);
			continue;
		}

		if (stage == RUNNING && input_quit_read(&u->in, &q)) {
			stage = GRACE;
			deadline = now_ms() + QUIT_GRACE_MS;
			fprintf(stderr, "quit combo: waiting for %d to leave\n", (int)pid);
			continue;
		}
		if (stage != RUNNING)
			input_quit_read(&u->in, &q);   /* keep the queues empty */

		if (stage != RUNNING && now_ms() >= deadline) {
			if (stage == GRACE) {
				/* The emulator, not runemu.sh: its cleanup after the
				 * emulator returns has to run. */
				int k = quit_signal_group(pid, pid, SIGTERM);
				fprintf(stderr, "quit combo: SIGTERM to %d process(es)\n", k);
				stage = TERMED;
				deadline = now_ms() + QUIT_TERM_MS;
			} else if (stage == TERMED) {
				int k = quit_signal_group(pid, pid, SIGKILL);
				fprintf(stderr, "quit combo: SIGKILL to %d process(es)\n", k);
				stage = KILLED;
				deadline = now_ms() + QUIT_TERM_MS;
			} else {
				/* Even the cleanup is stuck. The panel matters more. */
				fprintf(stderr, "quit combo: killing the whole group\n");
				kill(-pid, SIGKILL);
				deadline = now_ms() + QUIT_TERM_MS;
			}
		}
	}

	input_quit_only(&u->in, 0);
	if (pidfd >= 0)
		close(pidfd);
}

/* Takes the panel back after a child. The program that had it can still be
 * tearing down when its parent has already exited - gmu was still saving
 * its playlist a second after runemu.sh returned - and drmSetMaster says
 * EBUSY until it has closed the device. One attempt left the launcher
 * without a display for good; so it retries for a while here, and if that
 * is not enough, marks the panel lost and keeps trying from the main loop. */
/* Writes the chosen profile into the display controller again. Mesa's
 * display WSI clears the CRTC's color properties when a Vulkan program takes
 * the display (PortareOS patches that out), and a disabled CRTC may come
 * back without them; whenever the panel is ours again, the profile is
 * written again. Stock needs nothing. */
static void reapply_profile(struct ui *u)
{
	if (u->profile && u->profile_ok[u->profile] &&
	    kms_color_apply(&u->kms, &u->profiles[u->profile]) < 0)
		fprintf(stderr, "color: the profile did not take after the panel came back\n");
}

static int reclaim_panel(struct ui *u, int patience_ms)
{
	long long until = now_ms() + patience_ms;
	while (kms_set_master(&u->kms) < 0) {
		int err = errno;
		if (now_ms() >= until) {
			if (!u->panel_lost)
				fprintf(stderr, "set master: %s - retrying\n", strerror(err));
			u->panel_lost = 1;
			return -1;
		}
		struct timespec ts = { 0, 50 * 1000000L };
		nanosleep(&ts, NULL);
	}
	if (u->panel_lost)
		fprintf(stderr, "set master: back\n");
	u->panel_lost = 0;
	kms_present(&u->kms);
	reapply_profile(u);
	term_invalidate(&u->term);   /* someone else owned the panel */
	return 0;
}

static void hand_over(struct ui *u, char *const argv[], const char *cwd)
{
	/* Without master there is nothing to hand over; start it anyway
	 * rather than refuse, since the panel is free for the child. */
	if (!u->panel_lost && kms_drop_master(&u->kms) < 0)
		return;

	pid_t pid = fork();
	if (pid == 0) {
		setpgid(0, 0);
		if (cwd && chdir(cwd) < 0)
			_exit(127);
		execv(argv[0], argv);
		_exit(127);
	}

	if (pid < 0) {
		fprintf(stderr, "fork: %s\n", strerror(errno));
	} else {
		setpgid(pid, pid);        /* both sides, so neither can race */
		wait_or_quit(u, pid);
	}

	reclaim_panel(u, 3000);
	input_drain(&u->in);         /* and everything pressed meanwhile */
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

	draw_launching(u, g->name, s->core[0] ? s->core : s->emulator);
	hand_over(u, argv, NULL);
}

/* Runs a script from the Tools folder, from inside that folder, the way
 * EmulationStation did - some of them use relative paths.
 *
 * Directly, not through /usr/bin/run as ES did: run stops ${UI_SERVICE}
 * before it starts anything, and the UI service is this program. */
static void run_tool(struct ui *u, const struct tool *tl)
{
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", u->tools.dir, tl->file);
	char *const argv[] = { (char *)"/bin/bash", path, NULL };

	draw_launching(u, tl->name, tl->file);
	hand_over(u, argv, u->tools.dir);
}

/* Joins the network the keyboard is for, or says why not. On success the
 * password is scrubbed from memory and the Wi-Fi list comes back showing it
 * connected; on failure the keyboard stays up with the text intact, because
 * the likeliest fix is one wrong character. */
static void join(struct ui *u, const char *password)
{
	char busy[96];
	snprintf(busy, sizeof(busy), "joining %s...", u->join_ssid);
	draw_busy(u, busy);

	int rc = net_join(u->join_ssid, password);

	switch (rc) {
	case 0:
		memset(u->osk.text, 0, sizeof(u->osk.text));
		u->osk.len = 0;
		net_scan(&u->nets, 0);
		u->wifi_sel = u->nets.n > 0 ? 1 : 0;
		u->wifi_top = 0;
		u->screen = SCR_WIFI;
		return;
	case 4:
		str_copy(u->note, sizeof(u->note),
		         "Wrong password, or the network refused it.");
		break;
	case 10:
		str_copy(u->note, sizeof(u->note), "The network is out of range now.");
		break;
	case 3:
	case PROC_TIMEOUT:
		str_copy(u->note, sizeof(u->note),
		         "No answer from the network. Try closer to it.");
		break;
	default:
		snprintf(u->note, sizeof(u->note), "Could not join (nmcli error %d).", rc);
		break;
	}
}

/* Re-reads es_systems.cfg and every rom folder, keeping the selection on
 * the same system by name. Without this the catalogue was read once, at
 * start: a film copied over ssh while the launcher ran stayed invisible
 * until a restart. A full reload measured 5-7 ms on the device, so it is
 * simply done whenever a list is about to be shown. If the reload fails -
 * no system with games any more - the old catalogue stays rather than
 * leaving nothing to show. */
static void refresh_catalog(struct ui *u)
{
	char keep[64] = "";
	int on_tools = on_tools_row(u);
	struct catalog fresh;

	if (u->sys_sel < u->cat.n)
		str_copy(keep, sizeof(keep), u->cat.sys[u->sys_sel].name);

	if (catalog_load(&fresh, ES_SYSTEMS, SETTINGS) < 0)
		return;
	catalog_free(&u->cat);
	u->cat = fresh;
	tools_load(&u->tools);

	u->sys_sel = 0;
	if (on_tools && u->tools.n > 0) {
		u->sys_sel = u->cat.n;
		return;
	}
	for (int i = 0; i < u->cat.n; i++)
		if (strcmp(u->cat.sys[i].name, keep) == 0)
			u->sys_sel = i;
}

/* Back to the systems list, current as of now. */
static void show_systems(struct ui *u)
{
	refresh_catalog(u);
	u->screen = SCR_SYSTEMS;
}

static void on_action(struct ui *u, enum action a)
{

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

	/* A reboot or poweroff that was accepted but has not happened: the
	 * press that follows brings the panel back, and does nothing else. */
	if (u->going_down) {
		u->going_down = 0;
		kms_present(&u->kms);
		term_invalidate(&u->term);
		return;
	}

	/* Home is the way back to the consoles list from anywhere: a
	 * settings page, the keyboard with half a password in it, a power
	 * choice waiting for its second press. Whatever was pending is
	 * dropped, not applied. */
	if (a == ACT_HOME) {
		u->power_armed = -1;
		show_systems(u);
		return;
	}

	/* Only the keyboard tells START from the settings button. */
	if (a == ACT_START && u->screen != SCR_KEYBOARD)
		a = ACT_MENU;

	u->note[0] = '\0';

	switch (u->screen) {
	case SCR_SYSTEMS:
		if (a == ACT_UP && u->sys_sel > 0) u->sys_sel--;
		else if (a == ACT_DOWN && u->sys_sel < system_rows(u) - 1) u->sys_sel++;
		else if (a == ACT_CONFIRM && on_tools_row(u)) {
			/* Re-read on the way in: a package or an update may have
			 * changed the folder since start, and it costs one readdir. */
			tools_load(&u->tools);
			u->tool_sel = u->tool_top = 0;
			u->screen = SCR_TOOLS;
		}
		else if (a == ACT_CONFIRM) {
			refresh_catalog(u);
			if (u->sys_sel < u->cat.n) {
				u->screen = SCR_GAMES;
				u->game_sel = u->game_top = 0;
			}
		} else if (a == ACT_MENU) {
			u->screen = SCR_SETTINGS;
			u->set_sel = 0;
			u->ssh_on = net_ssh_enabled();
		} else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_GAMES: {
		const struct psystem *s = &u->cat.sys[u->sys_sel];
		if (a == ACT_UP && u->game_sel > 0) u->game_sel--;
		else if (a == ACT_DOWN && u->game_sel < s->ngames - 1) u->game_sel++;
		else if (a == ACT_CONFIRM) launch(u, s, &s->games[u->game_sel]);
		else if (a == ACT_BACK) show_systems(u);
		else if (a == ACT_QUIT) u->running = 0;
		break;
	}

	case SCR_TOOLS:
		if (a == ACT_UP && u->tool_sel > 0) u->tool_sel--;
		else if (a == ACT_DOWN && u->tool_sel < u->tools.n - 1) u->tool_sel++;
		else if (a == ACT_CONFIRM && u->tool_sel < u->tools.n)
			run_tool(u, &u->tools.t[u->tool_sel]);
		else if (a == ACT_BACK) show_systems(u);
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_SETTINGS:
		if (a == ACT_UP && u->set_sel > 0) u->set_sel--;
		else if (a == ACT_DOWN && u->set_sel < N_SETTINGS - 1) u->set_sel++;
		else if (a == ACT_CONFIRM && u->set_sel == SET_WIFI) {
			draw_busy(u, "scanning...");
			u->wifi_on = net_wifi_enabled();
			if (u->wifi_on)
				net_scan(&u->nets, 1);
			else
				memset(&u->nets, 0, sizeof(u->nets));
			/* On the first network when there are some, on the switch
			 * when there is nothing else to pick. */
			u->wifi_sel = (u->wifi_on && u->nets.n > 0) ? 1 : 0;
			u->wifi_top = 0;
			u->screen = SCR_WIFI;
		}
		else if (a == ACT_CONFIRM && u->set_sel == SET_ABOUT)
			open_about(u);
		else if (a == ACT_CONFIRM && u->set_sel == SET_TIMEZONE)
			open_tz(u);
		else if (a == ACT_CONFIRM && u->set_sel == SET_POWER) {
			u->power_sel = 0;
			u->power_armed = -1;
			u->screen = SCR_POWER;
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
		         u->set_sel == SET_SSH) {
			int on = !u->ssh_on;
			draw_busy(u, on ? "starting ssh..." : "stopping ssh...");
			net_ssh_set(on);
			/* Written as well, so the next boot agrees with the switch:
			 * the boot starts or stops sshd from this setting. */
			settings_set(SETTINGS, "ssh.enabled", on ? "1" : "0");
			u->ssh_on = net_ssh_enabled();
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
		         u->set_sel == SET_COLOR) {
			int n = term_palette_count();
			int cur = term_palette(&u->term);
			int next = (a == ACT_LEFT) ? (cur + n - 1) % n : (cur + 1) % n;
			u->pal_name = term_set_palette(&u->term, next);
			term_invalidate(&u->term);   /* every cell changes color */
			settings_set(SETTINGS, KEY_PALETTE, u->pal_name);
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_PROFILE) {
			/* Cycle, skipping profiles whose file is missing; stock is
			 * always there. Left goes the other way round. */
			int next = u->profile;
			do {
				next = (a == ACT_LEFT) ? (next + N_PROFILES - 1) % N_PROFILES
				                       : (next + 1) % N_PROFILES;
			} while (!u->profile_ok[next]);
			if (next != u->profile) {
				if (kms_color_apply(&u->kms, next ? &u->profiles[next] : NULL) == 0) {
					u->profile = next;
					settings_set(SETTINGS, KEY_PROFILE, profile_names[next].key);
				} else {
					snprintf(u->note, sizeof(u->note), "the display refused the profile");
				}
			}
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_CHARGING) {
			u->charging_led = !u->charging_led;
			settings_set(SETTINGS, KEY_CHARGING, u->charging_led ? "1" : "0");
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_BUTTONS) {
			u->retroid = !u->retroid;
			input_set_layout(u->retroid);
			/* Written straight away rather than on exit: this program
			 * can be killed from the outside and the setting that
			 * decides how to leave it should not be the one that is
			 * lost. */
			settings_set(SETTINGS, KEY_BUTTONS,
			             u->retroid ? "retroid" : "ps");
		}
		else if (a == ACT_BACK || a == ACT_MENU) show_systems(u);
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_WIFI:
		if (a == ACT_UP && u->wifi_sel > 0) u->wifi_sel--;
		else if (a == ACT_DOWN && u->wifi_on && u->wifi_sel < u->nets.n) u->wifi_sel++;
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->wifi_sel == 0) {
			int on = !u->wifi_on;
			draw_busy(u, on ? "switching Wi-Fi on..." : "switching Wi-Fi off...");
			net_wifi_set(on);
			/* Kept, because 080-network applies it at every boot. */
			settings_set(SETTINGS, "wifi.enabled", on ? "1" : "0");
			if (on) {
				/* The radio takes a moment after the block is lifted;
				 * a scan asked for before then finds nothing. */
				for (int i = 0; i < 20 && !net_wifi_enabled(); i++) {
					struct timespec ts = { 0, 250 * 1000000L };
					nanosleep(&ts, NULL);
				}
				draw_busy(u, "scanning...");
				struct timespec settle = { 1, 500 * 1000000L };
				nanosleep(&settle, NULL);
			}
			u->wifi_on = net_wifi_enabled();
			if (u->wifi_on)
				net_scan(&u->nets, 1);
			else
				memset(&u->nets, 0, sizeof(u->nets));
			if (on && !u->wifi_on)
				str_copy(u->note, sizeof(u->note), "Wi-Fi did not come on.");
			u->wifi_top = 0;
		}
		else if (a == ACT_MENU && u->wifi_on) {
			draw_busy(u, "rescanning...");
			net_scan(&u->nets, 1);
			if (u->wifi_sel > u->nets.n) u->wifi_sel = u->nets.n;
		}
		else if (a == ACT_CONFIRM && u->wifi_sel >= 1 && u->wifi_sel - 1 < u->nets.n) {
			const struct net_entry *e = &u->nets.e[u->wifi_sel - 1];
			int min = net_min_password(e->security);
			if (!e->saved && min < 0) {
				str_copy(u->note, sizeof(u->note),
				         "Needs a username too - not supported yet.");
			} else if (!e->saved) {
				str_copy(u->join_ssid, sizeof(u->join_ssid), e->name);
				osk_init(&u->osk, min);
				if (min == 0)
					join(u, "");   /* open: nothing to type */
				else
					u->screen = SCR_KEYBOARD;
			} else {
				draw_busy(u, "connecting...");
				net_connect(e->name);
				net_scan(&u->nets, 0);
			}
		}
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_KEYBOARD:
		switch (osk_action(&u->osk, a)) {
		case OSK_DONE:
			join(u, u->osk.text);
			break;
		case OSK_CANCEL:
			u->screen = SCR_WIFI;
			break;
		case OSK_NONE:
			if (a == ACT_QUIT)
				u->running = 0;
			break;
		}
		break;

	case SCR_POWER:
		if (a == ACT_CONFIRM && u->power_armed == u->power_sel) {
			u->power_armed = -1;
			power(u, power_verbs[u->power_sel]);
			break;
		}
		u->power_armed = -1;
		if (a == ACT_UP && u->power_sel > 0) u->power_sel--;
		else if (a == ACT_DOWN && u->power_sel < 1) u->power_sel++;
		else if (a == ACT_CONFIRM) u->power_armed = u->power_sel;
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_TZ: {
		int count = u->tz_level ? u->tz_nidx : u->tz.nregions;
		if (a == ACT_UP && u->tz_sel > 0) u->tz_sel--;
		else if (a == ACT_DOWN && u->tz_sel < count - 1) u->tz_sel++;
		else if (a == ACT_CONFIRM && u->tz_level == 0 && u->tz_sel < count)
			tz_show_region(u, u->tz_sel);
		else if (a == ACT_CONFIRM && u->tz_level == 1 && u->tz_sel < count) {
			const char *zone = u->tz.zone[u->tz_idx[u->tz_sel]];
			draw_busy(u, "setting the time zone...");
			if (tz_apply(zone, SETTINGS, TZ_CACHE) == 0) {
				str_copy(u->tz_cur, sizeof(u->tz_cur), zone);
				status_read(&u->st);        /* the header clock, now */
				u->screen = SCR_SETTINGS;
			} else {
				str_copy(u->note, sizeof(u->note), "Could not save it.");
			}
		}
		else if (a == ACT_BACK && u->tz_level == 1) {
			u->tz_level = 0;
			u->tz_sel = u->tz_region;
			u->tz_top = 0;
		}
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;
	}

	case SCR_ABOUT:
		if (a == ACT_CONFIRM && u->about_sel == 0) open_update(u);
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_UPDATE:
		if (a == ACT_UP && u->upd_sel > 0) u->upd_sel--;
		else if (a == ACT_DOWN && u->upd_sel < 1) u->upd_sel++;
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->upd_sel == 0) {
			/* Two channels, so any press flips it, and the answer for
			 * the other channel is fetched straight away. */
			const char *next = strcmp(u->upd.channel, "release") == 0
			                 ? "nightly" : "release";
			update_set_channel(SETTINGS, next);
			draw_busy(u, "checking for updates...");
			update_check(&u->upd);
		}
		else if (a == ACT_CONFIRM && u->upd_sel == 1) {
			if (u->upd_staged) {
				restart(u);
			} else if (u->upd.state == UPD_AVAILABLE) {
				draw_download(0, 0, u);
				if (update_fetch(draw_download, u, u->note, sizeof(u->note)) == 0)
					u->upd_staged = 1;
			} else {
				draw_busy(u, "checking for updates...");
				update_check(&u->upd);
			}
		}
		else if (a == ACT_BACK) u->screen = SCR_ABOUT;
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

	if (input_open(&u.in) < 0) {
		term_free(&u.term);
		kms_close(&u.kms);
		catalog_free(&u.cat);
		return 1;
	}

	/* After input_open, never before: it starts by clearing the whole
	 * struct, aux_fd included. The other way round registered the pipe and
	 * then forgot it, so input_sense's notifications piled up unread and
	 * the header only caught up on the minute tick. */
	if (osd_open(&u.osd) == 0)
		input_set_aux(&u.in, u.osd.fd);

	fprintf(stderr, "portarelauncher: %ux%u, %ux%u grid, %d systems\n",
	        u.kms.mode.hdisplay, u.kms.mode.vdisplay,
	        u.term.cols, u.term.rows, u.cat.n);

	/* Grey unless Settings chose otherwise. Amber was tried first and read
	 * as yellow on this panel. Stored by name rather than by index, so
	 * reordering the palettes cannot quietly change anyone's choice. */
	u.pal_name = term_set_palette(&u.term, 0);
	char pal[32];
	if (settings_get(SETTINGS, KEY_PALETTE, pal, sizeof(pal))) {
		int found = 0;
		for (int i = 0; i < term_palette_count() && !found; i++) {
			const char *name = term_set_palette(&u.term, i);
			if (strcmp(name, pal) == 0) {
				u.pal_name = name;
				found = 1;
			}
		}
		if (!found)   /* a name from a build that had other palettes */
			u.pal_name = term_set_palette(&u.term, 0);
	}

	/* The color profile, before the first frame, so the launcher is the
	 * first thing shown through it. Master is ours here. A boot leaves
	 * the color blocks off, so "stock" needs nothing written. */
	{
		char prof[32] = "";
		u.profile_ok[0] = 1;
		/* A kernel that drives the de-gamma stage gets the three-stage
		 * file, <key>-igc.profile, when the image has one; otherwise
		 * the two-stage <key>.profile. */
		int igc = kms_has_degamma(&u.kms);
		for (int i = 1; i < N_PROFILES; i++) {
			char path[128];
			u.profile_ok[i] = 0;
			if (igc) {
				snprintf(path, sizeof(path), PROFILE_DIR "/%s-igc.profile",
				         profile_names[i].key);
				u.profile_ok[i] = color_load(path, &u.profiles[i]) == 0;
			}
			if (!u.profile_ok[i]) {
				snprintf(path, sizeof(path), PROFILE_DIR "/%s.profile",
				         profile_names[i].key);
				u.profile_ok[i] = color_load(path, &u.profiles[i]) == 0;
			}
		}
		settings_get(SETTINGS, KEY_PROFILE, prof, sizeof(prof));
		char chg[8] = "";
		settings_get(SETTINGS, KEY_CHARGING, chg, sizeof(chg));
		u.charging_led = strcmp(chg, "0") != 0;
		for (int i = 1; i < N_PROFILES; i++)
			if (u.profile_ok[i] && strcmp(prof, profile_names[i].key) == 0 &&
			    kms_color_apply(&u.kms, &u.profiles[i]) == 0)
				u.profile = i;
	}

	/* Default to the layout printed on this device rather than to the
	 * positional convention, which would put confirm on the button
	 * labelled B. */
	char style[32];
	/* "sony" is accepted as well as "ps" because it is what earlier builds
	 * wrote, and a setting that silently flips on upgrade is worse than a
	 * spare string comparison. */
	u.retroid = !(settings_get(SETTINGS, KEY_BUTTONS, style, sizeof(style)) &&
	              (strcmp(style, "ps") == 0 || strcmp(style, "sony") == 0));
	input_set_layout(u.retroid);
	/* Read what is cheap now and leave the scan until the Wi-Fi screen is
	 * opened: a rescan takes seconds and nothing on the first screen shows
	 * it. */
	tools_load(&u.tools);
	usb_modes(u.usb_opts, &u.n_usb, 8);
	usb_mode(u.usb, sizeof(u.usb));
	/* For the version on the About row. The file cannot change while the
	 * system runs; an update takes a reboot. */
	osinfo_read(OS_RELEASE, &u.os);
	u.upd_staged = update_staged(STAGE_DIR);
	net_scan(&u.nets, 0);
	u.bt_auto = bt_autoconnect();
	u.bt_on = bt_powered();
	if (u.bt_on)
		bt_list(&u.bt);

	u.screen = SCR_SYSTEMS;
	u.running = 1;
	status_read(&u.st);
	net_address(u.addr, sizeof(u.addr));
	u.started = now_ms();
	u.net_next = u.started + NET_POLL_FAST_MS;
	redraw(&u);

	while (u.running && !stop_requested) {
		/* Sleep until the minute turns over, until the overlay is due
		 * to come down, or until something is pressed. Nothing else
		 * wakes this program. */
		int idle = status_ms_to_next_minute();
		if (u.panel_lost && idle > 1000)
			idle = 1000;         /* keep asking for the panel back */
		int osd_left = osd_remaining(&u.osd);
		if (osd_left >= 0 && osd_left < idle)
			idle = osd_left;
		long long net_left = u.net_next - now_ms();
		if (net_left < idle)
			idle = net_left > 0 ? (int)net_left : 0;

		enum action a = input_wait(&u.in, idle);
		net_poll(&u);

		if (blank_requested) {
			int on = blank_requested > 0;
			blank_requested = 0;
			if (on) {
				kms_present(&u.kms);
				reapply_profile(&u);
				term_invalidate(&u.term);
				status_read(&u.st);
				redraw(&u);
			} else {
				kms_blank(&u.kms);
			}
			continue;
		}

		if (u.panel_lost) {
			if (reclaim_panel(&u, 0) == 0)
				redraw(&u);
			if (a == ACT_TICK || u.panel_lost)
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
