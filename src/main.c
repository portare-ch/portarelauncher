/* portarelauncher: KMS launcher for PortareOS. */
#include "bt.h"
#include "catalog.h"
#include "disc_titles.h"
#include "fit.h"
#include "lists.h"
#include "color.h"
#include "input.h"
#include "glyph.h"
#include "ja26.h"
#include "kms.h"
#include "lang.h"
#include "net.h"
#include "notify.h"
#include "osinfo.h"
#include "osk.h"
#include "proc.h"
#include "quit.h"
#include "settings.h"
#include "status.h"
#include "term.h"
#include "tools.h"
#include "tz.h"
#include "uevent.h"
#include "update.h"
#include "idle.h"
#include "orbit.h"
#include "timeutil.h"

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
#ifndef PORTSCOPE
#define PORTSCOPE   "/usr/bin/portscope"   /* a test build points elsewhere */
#endif
#define SETTINGS    "/storage/.config/system/configs/system.cfg"
#ifndef JA26_PATH
#define JA26_PATH   "/usr/share/portarelauncher/ja26.bin"
#endif

/* Color profiles use /usr/config/color/<key>.profile; stock disables correction. */
#define KEY_PROFILE "display.colorprofile"
/* Charging thumbstick LEDs, read by the system daemon. */
#define KEY_CHARGING "led.charging"
/* Menu idle timeout in minutes; only applies while the launcher owns the panel.
 * Short by default: the panel is OLED, and a menu left up wears in. */
#define KEY_BLANK "display.blankminutes"
static const int blank_minutes[] = { 1, 2, 5, 10, 15 };
#define N_BLANK ((int)(sizeof(blank_minutes) / sizeof(blank_minutes[0])))
#define BLANK_DEFAULT 1          /* index: 2 minutes */
/* Minutes after blanking before power-handler is asked to sleep.
 * It applies the power key rules, including declining while SSH is active. */
#define KEY_SLEEP "system.sleepminutes"
static const int sleep_minutes[] = { 5, 10, 15, 30 };
#define N_SLEEP ((int)(sizeof(sleep_minutes) / sizeof(sleep_minutes[0])))
#define POWER_HANDLER "/usr/lib/autostart/quirks/platforms/SM8550/bin/power-handler"
#define PROFILE_DIR "/usr/config/color"
static const struct { const char *key, *label; } profile_names[] = {
	{ "stock",   "stock" },
	{ "gamma22", "Gamma 2.2" },
	{ "srgb",    "sRGB" },
};
#define N_PROFILES ((int)(sizeof(profile_names) / sizeof(profile_names[0])))

/* Per-console pre-emptive frame, stored as <system>.preempt (0 or 1)
 * and read by setsettings.sh at launch. */
static const struct { const char *key, *label; } consoles[] = {
	{ "snes",    "SNES"             },
	{ "nes",     "NES"              },
	{ "psx",     "PlayStation"      },
	{ "gb",      "Game Boy"         },
	{ "gbc",     "Game Boy Color"   },
	{ "gba",     "Game Boy Advance" },
	{ "genesis", "Genesis"          },
	{ "neocd",   "Neo Geo CD"       },
};
#define N_CONSOLES ((int)(sizeof(consoles) / sizeof(consoles[0])))

/* Missing or invalid values default to off. */
static int console_latency(int i)
{
	char key[64], val[16];
	snprintf(key, sizeof(key), "%s.preempt", consoles[i].key);
	return settings_get(SETTINGS, key, val, sizeof(val)) &&
	       strcmp(val, "1") == 0;
}

static const char *console_mode_name(int i, int latency)
{
	(void)i;
	return tr(latency ? S_PRMPT_ON : S_PRMPT_OFF);
}

static void console_set_latency(int i, int latency)
{
	char key[64];
	snprintf(key, sizeof(key), "%s.preempt", consoles[i].key);
	settings_set(SETTINGS, key, latency ? "1" : "0");
}

#ifndef PL_VERSION
#define PL_VERSION "dev"   /* set by the Makefile, from VERSION or git */
#endif
#define SCALE       3          /* 8x16 glyphs at 3x is 53x20 on this panel */
#define PANEL_W     1280       /* the screen a display shows zoomed, boxed */
#define PANEL_H     960

#define LIST_TOP    5
#define LIST_MARGIN 3          /* rows kept below the list for the footer */

/* Quick Access rows precede the systems list. */
#define QUICK_ROWS  2
enum { QUICK_RECENT = 0, QUICK_FAVS = 1 };
#define LISTS_DIR   "/storage/.config/portarelauncher"
#define RECENT_FILE LISTS_DIR "/recent"
#define FAVS_FILE   LISTS_DIR "/favourites"
#define PS2_TITLES "/usr/share/portarelauncher/ps2-titles.tsv"
#define PSX_TITLES "/usr/share/portarelauncher/psx-titles.tsv"
#define PSP_TITLES "/usr/share/portarelauncher/psp-titles.tsv"
#define NEOCD_TITLES "/usr/share/portarelauncher/neocd-titles.tsv"
#define XBOX_TITLES "/usr/share/portarelauncher/xbox-titles.tsv"
#define DISC_CACHE  "/storage/.cache/portarelauncher/disc-identities"
#define DISC_HELPER "/usr/bin/ps-chd-id"

/* Selected titles pause, scroll one column per step, pause at the end, then reset. */
#define MARQUEE_WAIT_MS 1000
#define MARQUEE_STEP_MS 150
#define MARQUEE_END_MS  1500

enum screen { SCR_SYSTEMS, SCR_GAMES, SCR_SETTINGS, SCR_WIFI, SCR_BT,
              SCR_KEYBOARD, SCR_TOOLS, SCR_ABOUT, SCR_UPDATE,
              SCR_TZ, SCR_POWER, SCR_CONSOLES, SCR_RECENT, SCR_FAVS,
              SCR_DIAG, SCR_REGION };

struct ui {
	struct term term;
	struct kms kms;
	struct input in;
	struct catalog cat;
	struct disc_titles discs;

	const char *pal_name;
	struct color_profile profiles[N_PROFILES];
	int profile_ok[N_PROFILES];  /* loaded profiles; stock at index 0 is always available */
	int profile;         /* applied profile index */
	int charging_led;    /* defaults to on unless led.charging is 0 */
	int blank_idx;       /* index into blank_minutes */
	int sleep_idx;       /* index into sleep_minutes */
	long long sleep_retry; /* sleep retry deadline, ms */
	long long last_act;  /* last input time, ms */
	int blanked;         /* CRTC disabled by idle or SIGUSR1 */
	struct status st;
	struct notify notify;
	int display_fd;      /* display hotplug uevents, or -1 */

	struct net_list nets;
	int wifi_on;         /* last queried radio state */
	int wifi_sel, wifi_top;  /* row 0: switch; remaining rows: networks */
	int ssh_on;          /* refreshed when Settings opens */

	struct bt_list bt;
	int bt_on, bt_auto;
	int bt_sel, bt_top;

	struct osk osk;
	char join_ssid[80];

	struct tools tools;
	int tool_sel, tool_top;

	struct tzlist tz;
	int tz_idx[TZ_MAX];  /* zone indices for the displayed region */
	int tz_nidx;
	int tz_level;        /* 0: regions, 1: cities */
	int tz_region;
	int tz_sel, tz_top;
	char tz_cur[48];

	int power_sel;
	int power_armed;     /* armed row, or -1 */
	int console_sel;
	int region_sel;
	int going_down;      /* shutdown accepted; panel off */
	int panel_lost;      /* retrying DRM master after a child exits */

	struct osinfo os;
	char addr[40];       /* empty when offline */
	long long net_next;  /* next address refresh, ms */
	long long started;   /* startup time, ms */
	int about_sel;

	struct update_info upd;
	int upd_sel;
	int upd_staged;      /* verified update staged for next boot */
	/* Screen message retained until the next press; included in each redraw. */
	char note[192];
	char usb[24];
	char usb_opts[8][24];
	int n_usb;
	int retroid;         /* Retroid button labels/layout */
	enum screen screen;
	int sys_sel, sys_top;    /* rows 0 and 1 are Quick Access, then systems */
	int game_sel, game_top;
	int set_sel;
	int running;

	struct recents recent;
	struct favs favs;
	int list_sel, list_top;  /* selected game index (excludes headings), top visible row */
	int mq_phase;            /* 0: paused, 1: scrolling, 2: at end */
	int mq_off;              /* scroll offset in columns */
	long long mq_at;         /* phase start time, ms */
};

#define KEY_BUTTONS "launcher.buttons"
#define KEY_PALETTE "launcher.palette"

/* Thirteen settings fit rows 2..14; additional settings need a submenu. */
enum { SET_WIFI = 0, SET_SSH, SET_BLUETOOTH, SET_USB, SET_BUTTONS, SET_CONSOLES,
       SET_COLOR, SET_PROFILE, SET_CHARGING, SET_REGION, SET_DIAGNOSTICS,
       SET_ABOUT, SET_POWER, N_SETTINGS };

static const enum str settings_labels[N_SETTINGS] = {
	S_SET_WIFI, S_SET_SSH, S_SET_BT, S_SET_USB, S_SET_BUTTONS, S_SET_CONSOLES,
	S_SET_COLOR, S_SET_PROFILE, S_SET_CHARGING, S_SET_REGION, S_SET_DIAG,
	S_SET_ABOUT, S_SET_POWER,
};

static const char *crumb_of(char *buf, size_t n, const char *a, const char *b,
                            const char *c)
{
	if (c)
		snprintf(buf, n, "%s  >  %s  >  %s", a, b, c);
	else
		snprintf(buf, n, "%s  >  %s", a, b);
	return buf;
}

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

/* Scrolls only when the selection leaves the visible window. */
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

static void draw_frame_right(struct ui *u, const char *crumb, int status)
{
	struct term *t = &u->term;
	char right[64];

	term_clear(t);

	/* Game lists show only the clock; other screens include status when it fits. */
	int n = 0;
	if (status && u->st.volume >= 0)
		n += snprintf(right + n, sizeof(right) - n, "%s %d%%  ", tr(S_VOL), u->st.volume);
	if (status && u->st.brightness >= 0)
		n += snprintf(right + n, sizeof(right) - n, "%s %d%%  ", tr(S_BRI), u->st.brightness);
	if (status && u->st.capacity >= 0)
		n += snprintf(right + n, sizeof(right) - n, "%s %d%%  ",
		              tr(u->st.charging ? S_CHG : S_BAT), u->st.capacity);
	snprintf(right + n, sizeof(right) - n, "%s", u->st.clock);
	/* Keep the full breadcrumb ahead of the optional device status. */
	if (text_width(crumb) + text_width(right) + 4 > (int)t->cols)
		str_copy(right, sizeof(right), u->st.clock);
	term_puts_right(t, t->cols - 1, 0, right, ATTR_MID);

	/* Leave two columns between the breadcrumb and the variable-width status.
	 * MID like the status: the header stays put on every screen, and on an
	 * OLED panel what stays put and bright wears in first. */
	int room = (int)t->cols - 1 - text_width(right) - 2 - 1;
	char c[192];
	if (room > 0) {
		snprintf(c, sizeof(c), "%.*s", (int)text_fit(crumb, room), crumb);
		term_puts(t, 1, 0, c, ATTR_MID);
	}

	term_hline(t, 1, G_HLINE_D, ATTR_DIM);
	term_hline(t, t->rows - 2, G_HLINE, ATTR_DIM);
}

static void draw_frame(struct ui *u, const char *crumb)
{
	draw_frame_right(u, crumb, 1);
}

/* Face-button labels by position; hints select them by action. */
struct face {
	unsigned char bottom, right, top, left;
	unsigned char confirm, back, menu, fav;
};

static struct face face_of(int retroid)
{
	struct face f;
	if (retroid) {
		f.bottom = 'B'; f.right = 'A'; f.top = 'X'; f.left = 'Y';
		f.confirm = f.right; f.back = f.bottom;
		f.menu = f.top; f.fav = f.left;
	} else {
		f.bottom = G_MARK_CROSS; f.right = G_MARK_CIRCLE;
		f.top = G_MARK_TRIANGLE; f.left = G_MARK_SQUARE;
		f.confirm = f.bottom; f.back = f.right;
		f.menu = f.top; f.fav = f.left;
	}
	return f;
}

/* Truncates unselected titles with "..."; selected titles scroll by off columns. */
static void row_label(const char *name, int width, int selected, int off,
                      char *buf, size_t bsz)
{
	if (width < 4)
		width = 4;
	if (text_width(name) <= width) {
		str_copy(buf, bsz, name);
		return;
	}
	if (!selected) {
		snprintf(buf, bsz, "%.*s...", (int)text_fit(name, width - 3), name);
		return;
	}
	/* Column offsets keep wide glyphs for two ticks, then advance past them whole. */
	const char *from = text_at_col(name, off);
	snprintf(buf, bsz, "%.*s", (int)text_fit(from, width), from);
}

/* ---- the cross-system lists -------------------------------------------- */

/* Cross-system row: a day heading or game. Titles come from the catalog,
 * falling back to the filename for paths absent from it. */
struct xrow {
	int heading;
	char label[192];
	char right[8];
	const struct psystem *sys;
	const struct game *game;      /* NULL when the catalog lacks it */
	const char *path;
	const char *sysname;
};

#define XROWS_MAX 512
struct xlist {
	struct xrow r[XROWS_MAX];
	int n, ngames;
};
static struct xlist xl;

static int path_exists(const char *path)
{
	return access(path, F_OK) == 0;
}

static const struct psystem *system_named(const struct catalog *c, const char *name)
{
	for (int i = 0; i < c->n; i++)
		if (strcmp(c->sys[i].name, name) == 0)
			return &c->sys[i];
	return NULL;
}

/* Longest directory match distinguishes nested system paths. */
static const struct psystem *system_of_path(const struct catalog *c, const char *path)
{
	const struct psystem *best = NULL;
	size_t bestlen = 0;
	for (int i = 0; i < c->n; i++) {
		size_t n = strlen(c->sys[i].path);
		if (n > bestlen && strncmp(path, c->sys[i].path, n) == 0 &&
		    path[n] == '/') {
			best = &c->sys[i];
			bestlen = n;
		}
	}
	return best;
}

static const struct game *game_at(const struct psystem *s, const char *path)
{
	for (int i = 0; s && i < s->ngames; i++)
		if (strcmp(s->games[i].path, path) == 0)
			return &s->games[i];
	return NULL;
}

static void name_from_path(const char *path, char *out, size_t osz)
{
	const char *slash = strrchr(path, '/');
	str_copy(out, osz, slash ? slash + 1 : path);
	char *dot = strrchr(out, '.');
	if (dot && dot != out)
		*dot = '\0';
	text_compose(out);          /* a Mac names files decomposed */
}

static struct xrow *add_game_row(struct xlist *l, const struct ui *u,
                                 const char *sysname, const char *path)
{
	if (l->n >= XROWS_MAX)
		return NULL;
	struct xrow *x = &l->r[l->n++];
	memset(x, 0, sizeof(*x));
	x->sys = sysname ? system_named(&u->cat, sysname)
	                 : system_of_path(&u->cat, path);
	x->game = game_at(x->sys, path);
	x->path = path;
	x->sysname = x->sys ? x->sys->name : (sysname ? sysname : "");
	if (x->game)
		str_copy(x->label, sizeof(x->label), x->game->name);
	else
		name_from_path(path, x->label, sizeof(x->label));
	if (!x->game && disc_titles_eligible(x->sysname, path, 0)) {
		struct game g = {0};
		str_copy(g.path, sizeof(g.path), path);
		str_copy(g.name, sizeof(g.name), x->label);
		disc_titles_apply(&u->discs, x->sysname, &g);
		str_copy(x->label, sizeof(x->label), g.name);
	}
	short_system(x->sysname, lang_get() == LANG_JA, x->right, sizeof(x->right));
	l->ngames++;
	return x;
}

static void build_list(const struct ui *u, enum screen which, struct xlist *l)
{
	l->n = l->ngames = 0;
	if (which == SCR_RECENT) {
		long long now = (long long)time(NULL);
		char last[32] = "";
		for (int i = 0; i < u->recent.n; i++) {
			const struct recent *e = &u->recent.e[i];
			char day[32];
			day_label(e->when, now, lang_get() == LANG_JA, day, sizeof(day));
			if (strcmp(day, last) != 0 && l->n < XROWS_MAX) {
				struct xrow *h = &l->r[l->n++];
				memset(h, 0, sizeof(*h));
				h->heading = 1;
				str_copy(h->label, sizeof(h->label), day);
				str_copy(last, sizeof(last), day);
			}
			add_game_row(l, u, e->sys, e->path);
		}
	} else {
		for (int i = 0; i < u->favs.n; i++)
			add_game_row(l, u, NULL, u->favs.path[i]);
	}
}

/* Returns the n-th game and its display row, skipping headings. */
static struct xrow *list_game(struct xlist *l, int n, int *row)
{
	int seen = 0;
	for (int i = 0; i < l->n; i++) {
		if (l->r[i].heading)
			continue;
		if (seen++ == n) {
			if (row)
				*row = i;
			return &l->r[i];
		}
	}
	return NULL;
}

/* Prunes missing games when a list opens and saves only if changed. */
static void prune_lists(struct ui *u)
{
	if (recents_prune(&u->recent, path_exists) > 0)
		recents_save(&u->recent, RECENT_FILE);
	if (favs_prune(&u->favs, path_exists) > 0)
		favs_save(&u->favs, FAVS_FILE);
}

static void toggle_favourite(struct ui *u, const char *path)
{
	favs_toggle(&u->favs, path);
	favs_save(&u->favs, FAVS_FILE);
}

/* ---- the scrolling title ----------------------------------------------- */

/* Selected title and available columns; returns 0 on screens without a marquee. */
static int selected_label(const struct ui *u, const char **name, int *width)
{
	const struct term *t = &u->term;
	if (u->screen == SCR_GAMES) {
		const struct psystem *s = &u->cat.sys[u->sys_sel - QUICK_ROWS];
		if (u->game_sel >= s->ngames)
			return 0;
		*name = s->games[u->game_sel].name;
		*width = (int)t->cols - 4 - 2;
		return 1;
	}
	if (u->screen == SCR_RECENT || u->screen == SCR_FAVS) {
		build_list(u, u->screen, &xl);
		struct xrow *x = list_game(&xl, u->list_sel, NULL);
		if (!x)
			return 0;
		*name = x->label;
		*width = (int)t->cols - 4 - text_width(x->right) - 3;
		return 1;
	}
	return 0;
}

/* Scroll stop at a character boundary with the title end in view; 0 if it fits. */
static int marquee_overflow(const struct ui *u)
{
	const char *name;
	int width;
	if (!selected_label(u, &name, &width))
		return 0;
	return text_scroll_end(name, width);
}

static void marquee_reset(struct ui *u)
{
	u->mq_phase = 0;
	u->mq_off = 0;
	u->mq_at = now_ms();
}

/* Milliseconds until the title moves next, or -1 when nothing scrolls. */
static int marquee_left(const struct ui *u)
{
	if (!marquee_overflow(u))
		return -1;
	long long span = u->mq_phase == 0 ? MARQUEE_WAIT_MS
	               : u->mq_phase == 1 ? MARQUEE_STEP_MS : MARQUEE_END_MS;
	long long left = u->mq_at + span - now_ms();
	return left > 0 ? (int)left : 0;
}

static void marquee_tick(struct ui *u)
{
	int over = marquee_overflow(u);
	if (!over || marquee_left(u) > 0)
		return;
	if (u->mq_phase == 0) {
		u->mq_phase = 1;
	} else if (u->mq_phase == 1) {
		if (++u->mq_off >= over) {
			u->mq_off = over;
			u->mq_phase = 2;
		}
	} else {
		u->mq_off = 0;
		u->mq_phase = 0;
	}
	u->mq_at = now_ms();
}

static void draw_row(struct ui *u, unsigned y, int selected,
                     const char *label, const char *right)
{
	struct term *t = &u->term;
	int attr = selected ? ATTR_BRIGHT : ATTR_TEXT;

	/* The caret is a solid block of pixels where the selection rests; the
	 * bright label already marks the row. */
	if (selected)
		term_putc(t, 2, y, G_CARET, ATTR_TEXT);
	term_puts(t, 4, y, label, attr);
	if (right && *right)
		term_puts_right(t, t->cols - 2, y, right, selected ? ATTR_BRIGHT : ATTR_MID);
}

/* Tools follows the systems when its folder is nonempty. */
static int system_rows(const struct ui *u)
{
	return QUICK_ROWS + u->cat.n + (u->tools.n > 0);
}

static int on_tools_row(const struct ui *u)
{
	return u->tools.n > 0 && u->sys_sel == QUICK_ROWS + u->cat.n;
}

/* The system under the selection, or -1 on a Quick Access or Tools row. */
static int sys_index(const struct ui *u)
{
	int i = u->sys_sel - QUICK_ROWS;
	return i >= 0 && i < u->cat.n ? i : -1;
}

static void draw_systems(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, "PortareOS");

	/* Quick Access, a separator, then systems; no blank row on the 20-row grid. */
	term_puts(t, 2, 3, tr(S_QUICK), ATTR_MID);
	draw_row(u, 4, u->sys_sel == QUICK_RECENT, tr(S_RECENT), NULL);
	draw_row(u, 5, u->sys_sel == QUICK_FAVS, tr(S_FAVS), NULL);
	term_hline(t, 6, G_HLINE, ATTR_DIM);
	term_puts(t, 2, 7, tr(S_SYSTEMS), ATTR_MID);

	const unsigned top = 8;
	int rows = system_rows(u) - QUICK_ROWS;
	int visible = (int)(t->rows - 2) - (int)top;
	int sel = u->sys_sel - QUICK_ROWS;
	scroll_to(sel < 0 ? 0 : sel, &u->sys_top, rows, visible);

	for (int i = 0; i < visible && u->sys_top + i < rows; i++) {
		int idx = u->sys_top + i;
		if (idx == u->cat.n) {
			draw_row(u, top + (unsigned)i, idx == sel, tr(S_TOOLS), NULL);
			continue;
		}
		const struct psystem *s = &u->cat.sys[idx];
		draw_row(u, top + (unsigned)i, idx == sel,
		         lang_system(s->name, s->fullname), NULL);
	}

	if (u->cat.n == 0) {
		unsigned y = top + (u->tools.n > 0 ? 2 : 0);
		term_puts(t, 4, y, tr(S_NO_GAMES), ATTR_BRIGHT);
		term_puts(t, 4, y + 2, tr(S_ADD_GAMES), ATTR_MID);
		struct face f = face_of(u->retroid);
		snprintf(buf, sizeof(buf), tr(S_PRESS_REFRESH), f.confirm);
		term_puts(t, 4, y + 3, buf, ATTR_MID);
	}

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(S_HINT_SYSTEMS), f.confirm, f.menu);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* The footer shows FAVOURITE or REMOVE for the selected game. */
static void draw_games_footer(struct ui *u, int fav, const char *right)
{
	struct term *t = &u->term;
	struct face f = face_of(u->retroid);
	char buf[192];

	snprintf(buf, sizeof(buf), tr(S_HINT_GAMES),
	         f.confirm, f.back, f.fav, tr(fav ? S_REMOVE : S_FAVOURITE));
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
	if (right && *right)
		term_puts_right(t, t->cols - 2, t->rows - 1, right, ATTR_MID);
}

static void draw_games(struct ui *u)
{
	struct term *t = &u->term;
	const struct psystem *s = &u->cat.sys[sys_index(u)];
	char crumb[192], buf[192];

	snprintf(crumb, sizeof(crumb), "PortareOS  >  %s",
	         lang_system(s->name, s->fullname));
	draw_frame_right(u, crumb, 0);

	/* Use the rows between the header and footer; the count shares the footer. */
	int visible = (int)(t->rows - 3) - (LIST_TOP - 2);
	scroll_to(u->game_sel, &u->game_top, s->ngames, visible);

	int width = (int)t->cols - 4 - 2;
	for (int i = 0; i < visible && u->game_top + i < s->ngames; i++) {
		int selected = u->game_top + i == u->game_sel;
		char label[192];
		row_label(s->games[u->game_top + i].name, width, selected,
		          u->mq_off, label, sizeof(label));
		draw_row(u, (unsigned)(LIST_TOP + i - 2), selected, label, NULL);
	}

	snprintf(buf, sizeof(buf), "%d / %d", u->game_sel + 1, s->ngames);
	draw_games_footer(u, s->ngames > 0 &&
	                  favs_has(&u->favs, s->games[u->game_sel].path), buf);
}

/* Cross-system games with short system labels; recent games have day headings. */
static void draw_list(struct ui *u)
{
	struct term *t = &u->term;
	int recent = u->screen == SCR_RECENT;
	char crumb[192];

	snprintf(crumb, sizeof(crumb), "PortareOS  >  %s",
	         tr(recent ? S_RECENT : S_FAVS));
	draw_frame_right(u, crumb, 0);

	build_list(u, u->screen, &xl);
	if (u->list_sel >= xl.ngames)
		u->list_sel = xl.ngames > 0 ? xl.ngames - 1 : 0;

	const unsigned top = LIST_TOP - 2;
	int visible = (int)(t->rows - 2) - (int)top;
	int selrow = 0;
	struct xrow *cur = list_game(&xl, u->list_sel, &selrow);
	scroll_to(selrow, &u->list_top, xl.n, visible);

	for (int i = 0; i < visible && u->list_top + i < xl.n; i++) {
		struct xrow *x = &xl.r[u->list_top + i];
		unsigned y = top + (unsigned)i;
		if (x->heading) {
			term_puts(t, 1, y, x->label, ATTR_TEXT);
			continue;
		}
		int selected = x == cur;
		int width = (int)t->cols - 4 - text_width(x->right) - 3;
		char label[192];
		row_label(x->label, width, selected, u->mq_off, label, sizeof(label));
		draw_row(u, y, selected, label, x->right);
	}

	if (xl.ngames == 0) {
		term_puts(t, 4, top + 1, tr(recent ? S_NO_PLAYED : S_NO_FAVS), ATTR_BRIGHT);
		if (!recent) {
			struct face f = face_of(u->retroid);
			char hint[192];
			snprintf(hint, sizeof(hint), tr(S_ADD_FAV), f.fav);
			term_puts(t, 4, top + 3, hint, ATTR_MID);
		}
	}

	draw_games_footer(u, cur && (!recent || favs_has(&u->favs, cur->path)), NULL);
}

/* Face-button positions for the layout preview. */
static void draw_face(struct ui *u, unsigned y, int retroid)
{
	struct term *t = &u->term;
	struct face f = face_of(retroid);

	/* Highlight the confirm button: right for Retroid, bottom for shapes. */
#define FACE_ATTR(g) ((g) == f.confirm ? ATTR_BRIGHT : ATTR_TEXT)
	term_putc(t, 9,  y,     f.top,    FACE_ATTR(f.top));
	term_putc(t, 6,  y + 1, f.left,   FACE_ATTR(f.left));
	term_putc(t, 12, y + 1, f.right,  FACE_ATTR(f.right));
	term_putc(t, 9,  y + 2, f.bottom, FACE_ATTR(f.bottom));
#undef FACE_ATTR

	term_putc(t, 20, y,     f.confirm, ATTR_BRIGHT);
	term_puts(t, 22, y,     tr(S_FACE_CONFIRM), ATTR_MID);
	term_putc(t, 20, y + 1, f.back,    ATTR_TEXT);
	term_puts(t, 22, y + 1, tr(S_FACE_BACK), ATTR_MID);
	term_putc(t, 20, y + 2, f.menu,    ATTR_TEXT);
	term_puts(t, 22, y + 2, tr(S_FACE_SETTINGS), ATTR_MID);
}

static unsigned wrap_puts(struct term *t, unsigned x, unsigned y, unsigned width,
                          unsigned lines, const char *text, int attr);

static void draw_settings(struct ui *u)
{
	struct term *t = &u->term;

	draw_frame(u, tr(S_SETTINGS));

	char val[64];
	for (int i = 0; i < N_SETTINGS; i++) {
		const char *value = "";
		switch (i) {
		case SET_WIFI: {
			const char *ssid = NULL;
			for (int k = 0; k < u->nets.n; k++)
				if (u->nets.e[k].active)
					ssid = u->nets.e[k].name;
			/* The list is from the last scan; the address is live. */
			value = ssid ? ssid : u->addr[0] ? tr(S_CONNECTED)
			      : tr(net_wifi_enabled() ? S_DISCONNECTED : S_OFF);
			break;
		}
		case SET_SSH:
			value = tr(u->ssh_on ? S_ON : S_OFF);
			break;
		case SET_USB:
			snprintf(val, sizeof(val), "%s",
			         u->usb[0] ? lang_usb_mode(u->usb) : tr(S_UNKNOWN));
			value = val;
			break;
		case SET_BLUETOOTH: {
			const char *dev = NULL;
			for (int k = 0; k < u->bt.n; k++)
				if (u->bt.d[k].connected)
					dev = u->bt.d[k].name;
			value = dev ? dev : tr(u->bt_on ? S_NO_DEVICES : S_OFF);
			break;
		}
		case SET_BUTTONS:
			value = u->retroid ? "Retroid" : tr(S_SHAPES);
			break;
		case SET_CONSOLES: {
			int n = 0;
			for (int k = 0; k < N_CONSOLES; k++)
				n += console_latency(k);
			if (n == 0)
				value = tr(S_DEFAULTS);
			else {
				snprintf(val, sizeof(val), tr(S_N_CHANGED), n);
				value = val;
			}
			break;
		}
		case SET_COLOR:
			value = lang_palette(u->pal_name);
			break;
		case SET_PROFILE:
			value = u->profile ? profile_names[u->profile].label : tr(S_STOCK);
			break;
		case SET_CHARGING:
			value = tr(u->charging_led ? S_ON : S_OFF);
			break;
		case SET_POWER:
			value = "";
			break;
		case SET_REGION:
			/* Name each language in its own script. */
			value = lang_name(lang_get());
			break;
		case SET_ABOUT:
			if (u->upd_staged)
				value = tr(S_RESTART_TO_INSTALL);
			else if (u->os.version[0]) {
				snprintf(val, sizeof(val), "%s %s", u->os.version, u->os.build);
				value = val;
			} else
				value = tr(S_UNKNOWN);
			break;
		}
		draw_row(u, (unsigned)(2 + i), i == u->set_sel,
		         tr(settings_labels[i]), value);
	}

	/* The three-row button diagram also uses the description band's upper rule. */
	unsigned y = 2 + N_SETTINGS;
	if (u->set_sel != SET_BUTTONS)
		term_hline(t, y, G_HLINE, ATTR_DIM);

	switch (u->set_sel) {
	case SET_BUTTONS:
		draw_face(u, y, u->retroid);
		break;
	case SET_CONSOLES:
		wrap_puts(t, 4, y + 1, t->cols - 8, 2,
		          tr(S_DESC_CONSOLES), ATTR_DIM);
		break;
	case SET_WIFI: {
		char addr[40] = "";
		net_address(addr, sizeof(addr));
		if (addr[0]) {
			snprintf(val, sizeof(val), tr(S_ADDRESS_FMT), addr);
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
		break;
	case SET_PROFILE:
		/* CRTC correction also applies to games and video. */
		/* Corrections currently crush dark greys on the device; stock remains default. */
		wrap_puts(t, 4, y + 1, t->cols - 8, 2,
		          tr(S_DESC_PROFILE), ATTR_DIM);
		if (!u->profile_ok[1] && !u->profile_ok[2])
			term_puts(t, 4, y + 1, tr(S_NO_PROFILES), ATTR_MID);
		break;
	case SET_CHARGING:
		wrap_puts(t, 4, y + 1, t->cols - 8, 2,
		          tr(S_DESC_CHARGING), ATTR_DIM);
		break;
	case SET_BLUETOOTH:
		term_puts(t, 4, y + 1, tr(S_DESC_BT), ATTR_DIM);
		break;
	case SET_REGION:
		term_puts(t, 4, y + 1, tr(S_DESC_REGION), ATTR_DIM);
		break;
	case SET_POWER:
		term_puts(t, 4, y + 1, tr(S_DESC_POWER), ATTR_DIM);
		break;
	case SET_ABOUT:
		term_puts(t, 4, y + 1, tr(S_DESC_ABOUT), ATTR_DIM);
		break;
	case SET_USB: {
		char addr[40] = "";
		usb_address(addr, sizeof(addr));
		term_puts(t, 4, y + 1, tr(S_DESC_USB), ATTR_DIM);
		if (addr[0] && strcmp(u->usb, "network") == 0) {
			snprintf(val, sizeof(val), tr(S_ADDRESS_FMT), addr);
			term_puts(t, 4, y + 2, val, ATTR_MID);
		}
		break;
	}
	default:
		break;
	}

	{
		struct face f = face_of(u->retroid);
		char hint[192];
		snprintf(hint, sizeof(hint), tr(S_HINT_SETTINGS), f.confirm, f.back);
		term_puts(t, 1, t->rows - 1, hint, ATTR_MID);
	}
}

/* Per-console pre-emptive frame switches with a wrapped description. */
static void draw_consoles(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_CONSOLES), NULL));
	for (int i = 0; i < N_CONSOLES; i++)
		draw_row(u, (unsigned)(3 + i), i == u->console_sel,
		         lang_system(consoles[i].key, consoles[i].label),
		         console_mode_name(i, console_latency(i)));

	unsigned y = 3 + N_CONSOLES;
	term_hline(t, y, G_HLINE, ATTR_DIM);
	unsigned width = t->cols - 8, room = t->rows - 2 - (y + 1);
	unsigned r = wrap_puts(t, 4, y + 1, width, room, tr(S_EXPERIMENTAL), ATTR_DIM);
	wrap_puts(t, 4, y + 1 + r, width, room - r, tr(S_PRMPT_DESC), ATTR_DIM);

	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(S_HINT_CHANGE), f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* The language row names both languages so users can undo an accidental switch. */
static void draw_region(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192], zone[48];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_REGION), NULL));
	draw_row(u, 3, u->region_sel == 0, tr(S_LANGUAGE_ROW), lang_name(lang_get()));
	if (!settings_get(SETTINGS, TZ_KEY, zone, sizeof(zone)))
		str_copy(zone, sizeof(zone), "UTC");
	draw_row(u, 4, u->region_sel == 1, tr(S_TIMEZONE), zone);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(S_HINT_CHANGE), f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

static void draw_diag(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_DIAG), NULL));
	draw_row(u, 3, 1, "PortScope", NULL);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(S_HINT_OPEN), f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Saved networks connect directly; new networks open the password keyboard. */
static void draw_wifi(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_WIFI), NULL));

	draw_row(u, 3, u->wifi_sel == 0, tr(S_SET_WIFI), tr(u->wifi_on ? S_ON : S_OFF));

	if (!u->wifi_on) {
		term_puts(t, 4, LIST_TOP + 1, tr(S_WIFI_TURN_ON), ATTR_DIM);
	} else {
		term_puts(t, 2, LIST_TOP - 1, tr(S_NETWORKS), ATTR_MID);
		snprintf(buf, sizeof(buf), tr(S_N_FOUND), u->nets.n);
		term_puts_right(t, t->cols - 2, LIST_TOP - 1, buf, ATTR_MID);
	}

	int visible = (int)list_rows(u);
	int net_sel = u->wifi_sel - 1;          /* -1 while the switch is selected */
	scroll_to(net_sel < 0 ? 0 : net_sel, &u->wifi_top, u->nets.n, visible);

	for (int i = 0; u->wifi_on && i < visible && u->wifi_top + i < u->nets.n; i++) {
		const struct net_entry *e = &u->nets.e[u->wifi_top + i];
		if (e->active)
			snprintf(buf, sizeof(buf), "%s", tr(S_CONNECTED));
		else if (e->saved)
			snprintf(buf, sizeof(buf), "%s", tr(S_SAVED));
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
		snprintf(buf, sizeof(buf), tr(S_HINT_WIFI_SWITCH), f.confirm, f.back);
	else
		snprintf(buf, sizeof(buf), tr(S_HINT_WIFI), f.confirm, f.back, f.menu);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

#define BT_HEAD 2          /* the two toggle rows above the device list */
#define BT_LIST_TOP 7

static int bt_rows(const struct ui *u)
{
	return (int)u->term.rows - BT_LIST_TOP - 2;
}

static void draw_bt(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_BT), NULL));

	draw_row(u, 3, u->bt_sel == 0, "Bluetooth", tr(u->bt_on ? S_ON : S_OFF));
	draw_row(u, 4, u->bt_sel == 1, tr(S_AUTOCONNECT), tr(u->bt_auto ? S_YES : S_NO));
	term_hline(t, 5, G_HLINE, ATTR_DIM);

	term_puts(t, 2, 6, tr(S_DEVICES), ATTR_MID);
	snprintf(buf, sizeof(buf), tr(S_N_FOUND), u->bt.n);
	term_puts_right(t, t->cols - 2, 6, buf, ATTR_MID);

	int visible = bt_rows(u);
	int sel = u->bt_sel - BT_HEAD;
	scroll_to(sel < 0 ? 0 : sel, &u->bt_top, u->bt.n, visible);

	if (u->bt.n == 0)
		term_puts(t, 4, BT_LIST_TOP,
		          tr(u->bt_on ? S_BT_NONE : S_BT_OFF), ATTR_DIM);

	for (int i = 0; i < visible && u->bt_top + i < u->bt.n; i++) {
		const struct bt_device *d = &u->bt.d[u->bt_top + i];
		if (d->connected)
			snprintf(buf, sizeof(buf), "%s", tr(S_CONNECTED));
		else if (d->paired)
			snprintf(buf, sizeof(buf), "%s", tr(d->trusted ? S_PAIRED : S_NOT_TRUSTED));
		else
			snprintf(buf, sizeof(buf), "%s", tr(S_NEW));
		draw_row(u, (unsigned)(BT_LIST_TOP + i), u->bt_top + i == sel,
		         d->name, buf);
	}

	struct face f = face_of(u->retroid);
	const char *verb = tr(S_BT_CHANGE);
	if (sel >= 0 && sel < u->bt.n)
		verb = tr(u->bt.d[sel].connected ? S_BT_DISCONNECT : S_BT_CONNECT);
	snprintf(buf, sizeof(buf), tr(S_HINT_BT), f.confirm, verb, f.back, f.menu);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Wraps to width columns and at most lines rows. Breaks at spaces, or
 * at whole characters for Japanese. Returns rows used. */
static unsigned wrap_puts(struct term *t, unsigned x, unsigned y, unsigned width,
                          unsigned lines, const char *text, int attr)
{
	unsigned row = 0;
	char buf[384];
	while (*text && row < lines) {
		while (*text == ' ')
			text++;
		size_t len = strlen(text);
		size_t take = text_fit(text, (int)width);
		if (take < len && text[take] != ' ') {
			size_t sp = take;
			while (sp > 0 && text[sp] != ' ')
				sp--;
			/* Prefer a space unless the break falls within Japanese text. */
			if (sp > 0 && (unsigned char)text[take] < 0x80)
				take = sp;
		}
		if (take == 0)
			take = 1;                  /* never stall on a glyph too wide */
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
	char buf[192];

	draw_frame(u, tr(S_TOOLS));
	snprintf(buf, sizeof(buf), tr(S_N_FOUND), u->tools.n);
	term_puts(t, 2, 3, tr(S_TOOLS_HEAD), ATTR_MID);
	term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);

	/* Reserve description space for instructions on leaving the tool. */
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
	snprintf(buf, sizeof(buf), tr(S_HINT_TOOLS), f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Wi-Fi password frame around osk.c, with a count and error message. */
static void draw_keyboard(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_WIFI), NULL));
	/* Leave two columns after the localized label. */
	unsigned lw = term_puts(t, 2, 2, tr(S_NETWORK), ATTR_MID);
	term_puts(t, 2 + (lw + 2 > 9 ? lw + 2 : 9), 2, u->join_ssid, ATTR_TEXT);

	term_puts(t, 2, 3, tr(S_PASSWORD), ATTR_MID);
	if (u->osk.len < u->osk.min_len)
		snprintf(buf, sizeof(buf), tr(S_PW_COUNT_MIN), u->osk.len,
		         OSK_MAX, u->osk.min_len);
	else
		snprintf(buf, sizeof(buf), "%d / %d", u->osk.len, OSK_MAX);
	term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);

	unsigned y = osk_draw(&u->osk, t, 4);
	if (u->note[0])
		term_puts(t, 4, y + 1, u->note, ATTR_BRIGHT);
	else
		term_puts(t, 4, y + 1, tr(S_PW_SHOW), ATTR_DIM);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(S_HINT_KEYBOARD), f.confirm, f.back, f.menu);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* ---- power -------------------------------------------------------------- */

/* Blank before shutdown; restore the panel on failure or on the next press
 * if shutdown was accepted but has not completed. */
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
	snprintf(u->note, sizeof(u->note),
	         tr(strcmp(verb, "reboot") == 0 ? S_FAIL_RESTART : S_FAIL_OFF), rc);
}

/* Rows 0..1 cycle settings; rows 2..3 index power_verbs and require confirmation. */
#define POW_BLANK   0
#define POW_SLEEP   1
#define POW_RESTART 2
#define POW_OFF     3
#define N_POWER_ROWS 4
static const enum str power_rows[] = { S_SCREEN_OFF, S_SLEEP_AFTER, S_RESTART, S_POWER_OFF };
static const char *const power_verbs[] = { "reboot", "poweroff" };

static void draw_power(struct ui *u)
{
	struct term *t = &u->term;
	struct face f = face_of(u->retroid);
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_POWER), NULL));
	for (int i = 0; i < N_POWER_ROWS; i++) {
		char v[16];
		const char *value = NULL;
		if (i == POW_BLANK || i == POW_SLEEP) {
			snprintf(v, sizeof(v), tr(S_MIN), i == POW_BLANK
			         ? blank_minutes[u->blank_idx] : sleep_minutes[u->sleep_idx]);
			value = v;
		}
		draw_row(u, (unsigned)(3 + i), i == u->power_sel, tr(power_rows[i]), value);
	}
	term_hline(t, 3 + N_POWER_ROWS, G_HLINE, ATTR_DIM);

	if (u->power_sel == POW_BLANK || u->power_sel == POW_SLEEP)
		wrap_puts(t, 4, 3 + N_POWER_ROWS + 1, t->cols - 8, 3,
		          tr(u->power_sel == POW_BLANK ? S_DESC_BLANK : S_DESC_SLEEP), ATTR_DIM);

	/* A second press confirms; any other action disarms. */
	if (u->power_armed >= 0) {
		snprintf(buf, sizeof(buf),
		         tr(u->power_armed == POW_RESTART ? S_ARMED_RESTART : S_ARMED_OFF),
		         f.confirm);
		term_puts(t, 4, 3 + N_POWER_ROWS + 2, buf, ATTR_BRIGHT);
	}
	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	snprintf(buf, sizeof(buf), tr(S_HINT_SELECT), f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* ---- time zone ------------------------------------------------------------ */

static void draw_tz(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192], now[8];
	int visible = (int)list_rows(u);

	if (u->tz_level == 0) {
		draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SET_REGION), tr(S_TIMEZONE), NULL));
		term_puts(t, 2, 3, tr(S_REGION_HEAD), ATTR_MID);
		term_puts_right(t, t->cols - 2, 3, u->tz_cur, ATTR_MID);
		scroll_to(u->tz_sel, &u->tz_top, u->tz.nregions, visible);
		for (int i = 0; i < visible && u->tz_top + i < u->tz.nregions; i++) {
			const char *r = u->tz.region[u->tz_top + i];
			size_t n = strlen(r);
			int here = strncmp(u->tz_cur, r, n) == 0 &&
			           (u->tz_cur[n] == '/' || u->tz_cur[n] == '\0');
			draw_row(u, (unsigned)(LIST_TOP + i), u->tz_top + i == u->tz_sel,
			         r, here ? tr(S_CURRENT) : NULL);
		}
	} else {
		draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_TIMEZONE),
		                       u->tz.region[u->tz_region], NULL));
		term_puts(t, 2, 3, tr(S_CITY), ATTR_MID);
		snprintf(buf, sizeof(buf), "%d", u->tz_nidx);
		term_puts_right(t, t->cols - 2, 3, buf, ATTR_MID);
		scroll_to(u->tz_sel, &u->tz_top, u->tz_nidx, visible);
		for (int i = 0; i < visible && u->tz_top + i < u->tz_nidx; i++) {
			const char *zone = u->tz.zone[u->tz_idx[u->tz_top + i]];
			char city[48];
			tz_city(zone, city, sizeof(city));
			tz_clock(zone, now, sizeof(now));
			snprintf(buf, sizeof(buf), "%s%s%s", now,
			         strcmp(zone, u->tz_cur) == 0 ? "  " : "",
			         strcmp(zone, u->tz_cur) == 0 ? tr(S_CURRENT) : "");
			draw_row(u, (unsigned)(LIST_TOP + i), u->tz_top + i == u->tz_sel,
			         city, buf);
		}
	}

	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(u->tz_level ? S_HINT_SET : S_HINT_OPEN),
	         f.confirm, f.back);
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

/* Load zones on demand. */
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
		return tr(S_ACT_RESTART);
	if (u->upd.state == UPD_AVAILABLE)
		return tr(S_ACT_DOWNLOAD);
	return tr(S_ACT_CHECK);
}

static void draw_about(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192], val[16];
	const struct osinfo *o = &u->os;

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_ABOUT), NULL));

	const char *upd;
	if (u->upd_staged)
		upd = tr(S_RESTART_TO_INSTALL);
	else if (settings_get(SETTINGS, "updates.branch", val, sizeof(val)) &&
	         (!strcmp(val, "nightly") || !strcmp(val, "release")))
		upd = val;
	else
		upd = tr(S_AUTOMATIC);
	draw_row(u, 3, u->about_sel == 0, tr(S_UPDATE), upd);
	term_hline(t, 5, G_HLINE, ATTR_DIM);

	term_puts(t, 4, 7, tr(S_VERSION), ATTR_MID);
	snprintf(buf, sizeof(buf), "%s  %s", o->version[0] ? o->version : tr(S_UNKNOWN),
	         o->build);
	term_puts(t, 15, 7, buf, ATTR_TEXT);

	term_puts(t, 4, 8, tr(S_COMMIT), ATTR_MID);
	snprintf(buf, sizeof(buf), "%.7s  %s", o->commit[0] ? o->commit : "-",
	         o->branch);
	term_puts(t, 15, 8, buf, ATTR_TEXT);

	term_puts(t, 4, 9, tr(S_BUILT), ATTR_MID);
	term_puts(t, 15, 9, o->date[0] ? o->date : "-", ATTR_TEXT);

	term_puts(t, 4, 10, tr(S_DEVICE), ATTR_MID);
	snprintf(buf, sizeof(buf), "%s  %s", o->device, o->cpu);
	term_puts(t, 15, 10, buf, ATTR_TEXT);

	term_puts(t, 4, 11, tr(S_ADDRESS), ATTR_MID);
	term_puts(t, 15, 11, u->addr[0] ? u->addr : tr(S_OFFLINE), ATTR_TEXT);

	/* The device generates its root password on first boot (007-rootpw). */
	char pw[40];
	term_puts(t, 4, 12, tr(S_PASSWORD_L), ATTR_MID);
	term_puts(t, 15, 12,
	          settings_get(SETTINGS, "root.password", pw, sizeof(pw)) ? pw : "-",
	          ATTR_TEXT);

	term_puts(t, 4, 13, tr(S_LAUNCHER), ATTR_MID);
	term_puts(t, 15, 13, PL_VERSION, ATTR_TEXT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(S_HINT_OPEN), f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* One channel row and one action row reflecting the next update step. */
static void draw_update(struct ui *u)
{
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_ABOUT), tr(S_UPDATE)));
	draw_row(u, 3, u->upd_sel == 0, tr(S_CHANNEL),
	         u->upd.channel[0] ? u->upd.channel : tr(S_UNKNOWN));
	draw_row(u, 4, u->upd_sel == 1, update_action(u), NULL);
	term_hline(t, 6, G_HLINE, ATTR_DIM);

	term_puts(t, 4, 8, tr(S_INSTALLED), ATTR_MID);
	term_puts(t, 16, 8, u->upd.installed[0] ? u->upd.installed : "-", ATTR_TEXT);
	if (u->upd.tag[0]) {
		term_puts(t, 4, 9, tr(S_LATEST), ATTR_MID);
		snprintf(buf, sizeof(buf), "%s  %lld MB", u->upd.tag,
		         u->upd.size / (1024 * 1024));
		term_puts(t, 16, 9, buf, ATTR_TEXT);
	}

	const char *l1 = "", *l2 = "";
	if (u->upd_staged) {
		l1 = tr(S_UPD_VERIFIED);
		l2 = "";
	} else switch (u->upd.state) {
	case UPD_AVAILABLE: l1 = tr(S_UPD_AVAILABLE); break;
	case UPD_CURRENT:   l1 = tr(S_UPD_CURRENT); break;
	case UPD_NEWER:     l1 = tr(S_UPD_NEWER); break;
	case UPD_NONE:      l1 = tr(S_UPD_NONE); break;
	case UPD_ERROR:     l1 = u->upd.error; break;
	case UPD_UNKNOWN:   break;
	}
	term_puts(t, 4, 11, l1, ATTR_BRIGHT);
	term_puts(t, 4, 12, l2, ATTR_BRIGHT);

	if (!strcmp(u->upd.channel, "release")) {
		term_puts(t, 4, 14, tr(S_REL_1), ATTR_DIM);
		term_puts(t, 4, 15, tr(S_REL_2), ATTR_DIM);
	} else {
		term_puts(t, 4, 14, tr(S_NIGHT_1), ATTR_DIM);
		term_puts(t, 4, 15, tr(S_NIGHT_2), ATTR_DIM);
	}

	if (u->note[0])
		term_puts(t, 4, t->rows - 4, u->note, ATTR_BRIGHT);

	struct face f = face_of(u->retroid);
	snprintf(buf, sizeof(buf), tr(S_HINT_SELECT), f.confirm, f.back);
	term_puts(t, 1, t->rows - 1, buf, ATTR_MID);
}

/* Redraws once per download percentage point. */
static void draw_download(int pct, int verifying, void *ctx)
{
	struct ui *u = ctx;
	struct term *t = &u->term;
	char buf[192];

	draw_frame(u, crumb_of(buf, sizeof(buf), tr(S_SETTINGS), tr(S_SET_ABOUT), tr(S_UPDATE)));
	term_puts(t, 4, 4, tr(verifying ? S_VERIFYING : S_DOWNLOADING), ATTR_BRIGHT);
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
		snprintf(buf, sizeof(buf), tr(S_MB_OF), (int)(mb * pct / 100), (int)mb);
		term_puts(t, 4, 10, buf, ATTR_MID);
	}

	term_puts(t, 4, 13, tr(S_KEEP_NET), ATTR_DIM);
	term_puts(t, 4, 14, tr(S_RETRY_RESUME), ATTR_DIM);
	term_flush(t);
}

/* Refresh the address when About opens. */
static void open_about(struct ui *u)
{
	net_address(u->addr, sizeof(u->addr));
	u->upd_staged = update_staged(STAGE_DIR);
	u->about_sel = 0;
	u->screen = SCR_ABOUT;
}

static void open_update(struct ui *u)
{
	draw_busy(u, tr(S_CHECKING));
	update_check(&u->upd);
	u->upd_staged = update_staged(STAGE_DIR);
	u->upd_sel = 1;
	u->screen = SCR_UPDATE;
}

static void restart(struct ui *u)
{
	power(u, "reboot");
}

static void redraw(struct ui *u)
{
	/* The grid walks four glyph pixels across and one down. Japanese glyphs
	 * fill their rows, so a third of that there. The clock redraw each
	 * minute moves it a pixel. */
	const int s = (int)u->term.scale;
	int dx, dy;
	orbit_offset((long long)time(NULL) / 60, 4 * s, lang_get() == LANG_JA ? s / 3 : s, &dx, &dy);
	term_shift(&u->term, dx, dy);

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
	case SCR_CONSOLES: draw_consoles(u); break;
	case SCR_DIAG:     draw_diag(u);     break;
	case SCR_REGION:   draw_region(u);   break;
	case SCR_UPDATE:   draw_update(u);   break;
	case SCR_RECENT:
	case SCR_FAVS:     draw_list(u);     break;
	}
	term_flush(&u->term);
}

/* Show busy state before blocking network or USB commands. */
static void draw_busy(struct ui *u, const char *what)
{
	struct term *t = &u->term;
	term_puts(t, 4, t->rows - 4, what, ATTR_BRIGHT);
	term_flush(t);
}

static void draw_launching(struct ui *u, const char *what)
{
	struct term *t = &u->term;

	draw_frame(u, "PortareOS");
	term_puts(t, 6, 6, what, ATTR_BRIGHT);
	term_puts(t, 6, 8, tr(S_STARTING), ATTR_DIM);
	term_flush(t);
}

/* Children get their own process group for Home + START exit handling (quit.h).
 * The wait loop polls input and a pidfd to avoid periodic wakeups. */
#define QUIT_GRACE_MS 1500    /* for a program that quits on the combo itself */
#define QUIT_TERM_MS  5000    /* between SIGTERM and SIGKILL */

/* Poll the kernel address list quickly while offline after startup, then
 * once per minute. Redraw Settings or About when the address changes. */
#define NET_POLL_FAST_MS   3000
#define NET_POLL_SLOW_MS  60000
#define NET_POLL_FAST_FOR 300000   /* 5 minutes of quick looks after start */

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

/* Returns the child's wait status, or -1 when it could not be collected. */
static int wait_or_quit(struct ui *u, pid_t pid)
{
	enum { RUNNING, GRACE, TERMED, KILLED } stage = RUNNING;
	long long deadline = 0;
	int status = -1;
	struct quit_combo q;
	quit_reset(&q);

	int pidfd = open_pidfd(pid);
	input_quit_only(&u->in, 1);

	for (;;) {
		int st;
		pid_t r = waitpid(pid, &st, WNOHANG);
		if (r == pid) {
			status = st;
			break;
		}
		if (r < 0 && errno != EINTR)
			break;

		struct pollfd pfd[INPUT_MAX_DEV + 2];
		int ndev = input_fill_poll(&u->in, pfd);
		int n = ndev;
		if (pidfd >= 0)
			pfd[n++] = (struct pollfd){ .fd = pidfd, .events = POLLIN };

		/* Without pidfd support, check child exit every 500 ms. */
		int timeout = pidfd >= 0 ? -1 : 500;
		if (stage != RUNNING) {
			long long left = deadline - now_ms();
			int l = left > 0 ? (int)left : 0;
			if (timeout < 0 || l < timeout)
				timeout = l;
		}

		if (poll(pfd, (nfds_t)n, timeout) < 0 && errno != EINTR)
			break;

		/* Reopening devices can move their slots; discard the old combo state. */
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
				/* Leave runemu.sh alive to finish emulator cleanup. */
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
				/* Cleanup also timed out; terminate the remaining group. */
				fprintf(stderr, "quit combo: killing the whole group\n");
				kill(-pid, SIGKILL);
				deadline = now_ms() + QUIT_TERM_MS;
			}
		}
	}

	input_quit_only(&u->in, 0);
	if (pidfd >= 0)
		close(pidfd);
	return status;
}

/* The panel's screen on whatever is driven: the whole panel, or a
 * whole-number zoom of it boxed in a display's mode. */
static int screen_init(struct ui *u)
{
	struct kms *k = &u->kms;
	struct fit f = fit_box(k->mode.hdisplay, k->mode.vdisplay, PANEL_W, PANEL_H);
	return term_init(&u->term, k->map + (size_t)f.y * k->pitch_px + f.x,
	                 k->pitch_px, f.w, f.h, SCALE * (f.zoom ? f.zoom : 1));
}

/* Moves the menu to the output that is connected now. Returns 1 when it
 * moved; the screen is then blank and needs a redraw. */
static int follow_display(struct ui *u)
{
	int r = kms_follow(&u->kms);
	if (r < 0)
		fprintf(stderr, "display: nothing driven after a hotplug\n");
	if (r <= 0)
		return 0;
	term_free(&u->term);
	if (screen_init(u) < 0) {
		/* Restart=always opens the new output from scratch. */
		fprintf(stderr, "display: no screen fits %ux%u\n",
		        u->kms.mode.hdisplay, u->kms.mode.vdisplay);
		u->running = 0;
		return 0;
	}
	fprintf(stderr, "display: %ux%u, %ux%u grid\n", u->kms.mode.hdisplay,
	        u->kms.mode.vdisplay, u->term.cols, u->term.rows);
	return 1;
}

/* Reapply after reclaiming the panel: a modeset or another DRM master
 * may have cleared the CRTC color properties. */
static void reapply_profile(struct ui *u)
{
	if (u->profile && u->profile_ok[u->profile] &&
	    kms_color_apply(&u->kms, &u->profiles[u->profile]) < 0)
		fprintf(stderr, "color: the profile did not take after the panel came back\n");
}

/* A child may exit before its display teardown finishes. Retry EBUSY here,
 * then continue recovery from the main loop if necessary. */
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
	/* A display may have come or gone while the game had the screen. */
	if (!follow_display(u))
		kms_present(&u->kms);
	reapply_profile(u);
	term_set_dim(&u->term, 0);
	term_invalidate(&u->term);
	/* Restart idle timing so a long game does not immediately blank the menu. */
	u->last_act = now_ms();
	u->blanked = 0;
	return 0;
}

static int hand_over(struct ui *u, char *const argv[], const char *cwd)
{
	int status = -1;
	disc_titles_stop(&u->discs);

	/* If master is already lost, the panel is available to the child. */
	if (!u->panel_lost && kms_drop_master(&u->kms) < 0)
		return -1;

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
		setpgid(pid, pid);        /* set in parent and child to avoid a race */
		status = wait_or_quit(u, pid);
	}

	reclaim_panel(u, 3000);
	input_drain(&u->in);
	return status;
}

/* Build argv directly so game paths remain single arguments. */
static void launch(struct ui *u, const struct psystem *s, const struct game *g)
{
	char pflag[64], core[96], emu[96];

	/* Reject missing cores before RetroArch reports an unhelpful path error. */
	if (!s->core[0] || !s->emulator[0]) {
		struct term *t = &u->term;
		draw_frame(u, "PortareOS");
		term_puts(t, 4, 6, tr(S_CANNOT_LAUNCH), ATTR_BRIGHT);
		term_puts(t, 4, 7, tr(s->core[0] ? S_NO_EMULATOR : S_NO_CORE), ATTR_BRIGHT);
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

	draw_launching(u, g->name);
	int status = hand_over(u, argv, NULL);

	/* Record successful runemu exits; failed launches are not recent games. */
	if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
		recents_add(&u->recent, s->name, g->path, (long long)time(NULL));
		recents_save(&u->recent, RECENT_FILE);
	}
}

/* Launch the catalog entry, or fall back to its stored path. */
static void launch_row(struct ui *u, const struct xrow *x)
{
	if (!x->sys)
		return;
	if (x->game) {
		launch(u, x->sys, x->game);
		return;
	}
	struct game g;
	memset(&g, 0, sizeof(g));
	str_copy(g.name, sizeof(g.name), x->label);
	str_copy(g.path, sizeof(g.path), x->path);
	launch(u, x->sys, &g);
}

/* Run tools from their directory for relative paths. Avoid /usr/bin/run:
 * it stops UI_SERVICE, which is this launcher. */
static void run_tool(struct ui *u, const struct tool *tl)
{
	char path[256];
	snprintf(path, sizeof(path), "%s/%s", u->tools.dir, tl->file);
	char *const argv[] = { (char *)"/bin/bash", path, NULL };

	draw_launching(u, tl->name);
	hand_over(u, argv, u->tools.dir);
}

/* Ensure InputPlumber restarts even if PortScope was killed in raw mode
 * before its cleanup ran. Starting an active unit is harmless. */
static void run_portscope(struct ui *u)
{
	char *const argv[] = { (char *)PORTSCOPE, NULL };
	hand_over(u, argv, NULL);

	char *const ip[] = { (char *)"systemctl", (char *)"start",
	                     (char *)"inputplumber.service", NULL };
	proc_run_for(ip, NULL, NULL, 10000);
}

/* Clear the password and return to Wi-Fi on success; preserve keyboard
 * text on failure so it can be corrected. */
static void join(struct ui *u, const char *password)
{
	char busy[192];
	snprintf(busy, sizeof(busy), tr(S_JOINING), u->join_ssid);
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
		str_copy(u->note, sizeof(u->note), tr(S_PW_REJECTED));
		break;
	case 10:
		str_copy(u->note, sizeof(u->note), tr(S_OUT_OF_RANGE));
		break;
	case 3:
	case PROC_TIMEOUT:
		str_copy(u->note, sizeof(u->note), tr(S_NO_RESPONSE));
		break;
	default:
		snprintf(u->note, sizeof(u->note), tr(S_CONN_FAILED), rc);
		break;
	}
}

/* Reload before showing a list so newly copied games appear. Preserve the
 * selected system by name and retain the old catalog if loading fails. */
static void refresh_catalog(struct ui *u)
{
	disc_titles_stop(&u->discs);
	char keep[64] = "";
	int on_tools = on_tools_row(u);
	int quick = u->sys_sel < QUICK_ROWS ? u->sys_sel : -1;
	struct catalog fresh;

	if (sys_index(u) >= 0)
		str_copy(keep, sizeof(keep), u->cat.sys[sys_index(u)].name);

	if (catalog_load_titles(&fresh, ES_SYSTEMS, SETTINGS, &u->discs) < 0)
		return;
	catalog_free(&u->cat);
	u->cat = fresh;
	tools_load(&u->tools);

	if (quick >= 0) {
		u->sys_sel = quick;
		return;
	}
	u->sys_sel = QUICK_ROWS;
	if (on_tools && u->tools.n > 0) {
		u->sys_sel = QUICK_ROWS + u->cat.n;
		return;
	}
	for (int i = 0; i < u->cat.n; i++)
		if (strcmp(u->cat.sys[i].name, keep) == 0)
			u->sys_sel = QUICK_ROWS + i;
}

static void show_list(struct ui *u, enum screen which)
{
	prune_lists(u);
	u->list_sel = u->list_top = 0;
	u->screen = which;
	disc_titles_begin_catalog(&u->discs, &u->cat);
}

static void show_systems(struct ui *u)
{
	refresh_catalog(u);
	u->screen = SCR_SYSTEMS;
}

static void on_action(struct ui *u, enum action a)
{

	if (a == ACT_AUX) {
		/* Notifications trigger a settings refresh; their payload is unused. */
		notify_drain(&u->notify);
		status_read(&u->st);
		return;
	}

	if (a == ACT_TICK) {
		marquee_tick(u);
		status_read(&u->st);
		return;
	}

	/* Restart scrolling on each press so a new selection begins at its title start. */
	marquee_reset(u);

	/* If shutdown is still pending, the next press only restores the panel. */
	if (u->going_down) {
		u->going_down = 0;
		kms_present(&u->kms);
		term_invalidate(&u->term);
		return;
	}

	/* Home returns to systems and cancels pending edits or power confirmation. */
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
		else if (a == ACT_CONFIRM) {
			/* Reload even from Quick Access so the empty catalog can be refreshed. */
			refresh_catalog(u);
			if (u->sys_sel == QUICK_RECENT)
				show_list(u, SCR_RECENT);
			else if (u->sys_sel == QUICK_FAVS)
				show_list(u, SCR_FAVS);
			else if (on_tools_row(u)) {
				u->tool_sel = u->tool_top = 0;
				u->screen = SCR_TOOLS;
			} else if (sys_index(u) >= 0) {
				u->screen = SCR_GAMES;
				u->game_sel = u->game_top = 0;
				disc_titles_begin(&u->discs, &u->cat.sys[sys_index(u)]);
			}
		} else if (a == ACT_MENU) {
			u->screen = SCR_SETTINGS;
			u->set_sel = 0;
			u->ssh_on = net_ssh_enabled();
		} else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_GAMES: {
		const struct psystem *s = &u->cat.sys[sys_index(u)];
		if (a == ACT_UP && u->game_sel > 0) u->game_sel--;
		else if (a == ACT_DOWN && u->game_sel < s->ngames - 1) u->game_sel++;
		else if (a == ACT_CONFIRM) launch(u, s, &s->games[u->game_sel]);
		else if (a == ACT_FAV && s->ngames > 0)
			toggle_favourite(u, s->games[u->game_sel].path);
		else if (a == ACT_BACK) show_systems(u);
		else if (a == ACT_QUIT) u->running = 0;
		break;
	}

	case SCR_RECENT:
	case SCR_FAVS: {
		build_list(u, u->screen, &xl);
		struct xrow *x = list_game(&xl, u->list_sel, NULL);
		if (a == ACT_UP && u->list_sel > 0) u->list_sel--;
		else if (a == ACT_DOWN && u->list_sel < xl.ngames - 1) u->list_sel++;
		else if (a == ACT_CONFIRM && x) launch_row(u, x);
		else if (a == ACT_FAV && x) {
			/* The toggle rewrites list storage; copy the path first. */
			char path[512];
			str_copy(path, sizeof(path), x->path);
			toggle_favourite(u, path);
		}
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
			draw_busy(u, tr(S_SCANNING));
			u->wifi_on = net_wifi_enabled();
			if (u->wifi_on)
				net_scan(&u->nets, 1);
			else
				memset(&u->nets, 0, sizeof(u->nets));
			u->wifi_sel = (u->wifi_on && u->nets.n > 0) ? 1 : 0;
			u->wifi_top = 0;
			u->screen = SCR_WIFI;
		}
		else if (a == ACT_CONFIRM && u->set_sel == SET_ABOUT)
			open_about(u);
		else if (a == ACT_CONFIRM && u->set_sel == SET_REGION) {
			u->region_sel = 0;
			u->screen = SCR_REGION;
		}
		else if (a == ACT_CONFIRM && u->set_sel == SET_POWER) {
			u->power_sel = 0;
			u->power_armed = -1;
			u->screen = SCR_POWER;
		}
		else if (a == ACT_CONFIRM && u->set_sel == SET_CONSOLES) {
			u->console_sel = 0;
			u->screen = SCR_CONSOLES;
		}
		else if (a == ACT_CONFIRM && u->set_sel == SET_DIAGNOSTICS)
			u->screen = SCR_DIAG;
		else if (a == ACT_CONFIRM && u->set_sel == SET_BLUETOOTH) {
			draw_busy(u, tr(S_READING_DEVICES));
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
			draw_busy(u, tr(on ? S_SSH_STARTING : S_SSH_STOPPING));
			net_ssh_set(on);
			/* Persist for the boot-time sshd switch. */
			settings_set(SETTINGS, "ssh.enabled", on ? "1" : "0");
			u->ssh_on = net_ssh_enabled();
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_USB && u->n_usb > 0) {
			/* Use the modes reported by usbgadget --options. */
			int cur = 0;
			for (int i = 0; i < u->n_usb; i++)
				if (strcmp(u->usb_opts[i], u->usb) == 0)
					cur = i;
			int next = (a == ACT_LEFT)
			         ? (cur + u->n_usb - 1) % u->n_usb
			         : (cur + 1) % u->n_usb;
			draw_busy(u, tr(S_SWITCHING));
			usb_set_mode(u->usb_opts[next]);
			usb_mode(u->usb, sizeof(u->usb));
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_COLOR) {
			int n = term_palette_count();
			int cur = term_palette(&u->term);
			int next = (a == ACT_LEFT) ? (cur + n - 1) % n : (cur + 1) % n;
			u->pal_name = term_set_palette(&u->term, next);
			term_invalidate(&u->term);
			settings_set(SETTINGS, KEY_PALETTE, u->pal_name);
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->set_sel == SET_PROFILE) {
			/* Skip missing profile files; stock is always available. */
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
					str_copy(u->note, sizeof(u->note), tr(S_PROFILE_FAIL));
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
			/* Persist immediately so an external kill cannot lose the button layout. */
			settings_set(SETTINGS, KEY_BUTTONS,
			             u->retroid ? "retroid" : "shapes");
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
			draw_busy(u, tr(on ? S_WIFI_ON_BUSY : S_WIFI_OFF_BUSY));
			net_wifi_set(on);
			/* 080-network reapplies this at boot. */
			settings_set(SETTINGS, "wifi.enabled", on ? "1" : "0");
			if (on) {
				/* Allow the radio to become ready before scanning. */
				for (int i = 0; i < 20 && !net_wifi_enabled(); i++) {
					struct timespec ts = { 0, 250 * 1000000L };
					nanosleep(&ts, NULL);
				}
				draw_busy(u, tr(S_SCANNING));
				struct timespec settle = { 1, 500 * 1000000L };
				nanosleep(&settle, NULL);
			}
			u->wifi_on = net_wifi_enabled();
			if (u->wifi_on)
				net_scan(&u->nets, 1);
			else
				memset(&u->nets, 0, sizeof(u->nets));
			if (on && !u->wifi_on)
				str_copy(u->note, sizeof(u->note), tr(S_WIFI_FAIL));
			u->wifi_top = 0;
		}
		else if (a == ACT_MENU && u->wifi_on) {
			draw_busy(u, tr(S_RESCANNING));
			net_scan(&u->nets, 1);
			if (u->wifi_sel > u->nets.n) u->wifi_sel = u->nets.n;
		}
		else if (a == ACT_CONFIRM && u->wifi_sel >= 1 && u->wifi_sel - 1 < u->nets.n) {
			const struct net_entry *e = &u->nets.e[u->wifi_sel - 1];
			int min = net_min_password(e->security);
			if (!e->saved && min < 0) {
				str_copy(u->note, sizeof(u->note), tr(S_NO_ENTERPRISE));
			} else if (!e->saved) {
				str_copy(u->join_ssid, sizeof(u->join_ssid), e->name);
				osk_init(&u->osk, min);
				if (min == 0)
					join(u, "");   /* open: nothing to type */
				else
					u->screen = SCR_KEYBOARD;
			} else {
				draw_busy(u, tr(S_CONNECTING));
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
		if (a == ACT_CONFIRM && u->power_sel >= POW_RESTART &&
		    u->power_armed == u->power_sel) {
			u->power_armed = -1;
			power(u, power_verbs[u->power_sel - POW_RESTART]);
			break;
		}
		u->power_armed = -1;
		if (a == ACT_UP && u->power_sel > 0) u->power_sel--;
		else if (a == ACT_DOWN && u->power_sel < N_POWER_ROWS - 1) u->power_sel++;
		else if (u->power_sel == POW_BLANK &&
		         (a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT)) {
			char buf[8];
			u->blank_idx = (a == ACT_LEFT)
			             ? (u->blank_idx + N_BLANK - 1) % N_BLANK
			             : (u->blank_idx + 1) % N_BLANK;
			snprintf(buf, sizeof(buf), "%d", blank_minutes[u->blank_idx]);
			settings_set(SETTINGS, KEY_BLANK, buf);
		}
		else if (u->power_sel == POW_SLEEP &&
		         (a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT)) {
			char buf[8];
			u->sleep_idx = (a == ACT_LEFT)
			             ? (u->sleep_idx + N_SLEEP - 1) % N_SLEEP
			             : (u->sleep_idx + 1) % N_SLEEP;
			snprintf(buf, sizeof(buf), "%d", sleep_minutes[u->sleep_idx]);
			settings_set(SETTINGS, KEY_SLEEP, buf);
		}
		else if (a == ACT_CONFIRM) u->power_armed = u->power_sel;
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_CONSOLES:
		if (a == ACT_UP && u->console_sel > 0) u->console_sel--;
		else if (a == ACT_DOWN && u->console_sel < N_CONSOLES - 1) u->console_sel++;
		else if (a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) {
			int i = u->console_sel;
			console_set_latency(i, !console_latency(i));
		}
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
			draw_busy(u, tr(S_SAVING_TZ));
			if (tz_apply(zone, SETTINGS, TZ_CACHE) == 0) {
				str_copy(u->tz_cur, sizeof(u->tz_cur), zone);
				status_read(&u->st);
				u->screen = SCR_REGION;
			} else {
				str_copy(u->note, sizeof(u->note), tr(S_TZ_FAIL));
			}
		}
		else if (a == ACT_BACK && u->tz_level == 1) {
			u->tz_level = 0;
			u->tz_sel = u->tz_region;
			u->tz_top = 0;
		}
		else if (a == ACT_BACK) u->screen = SCR_REGION;
		else if (a == ACT_QUIT) u->running = 0;
		break;
	}

	case SCR_REGION:
		if (a == ACT_UP && u->region_sel > 0) u->region_sel--;
		else if (a == ACT_DOWN && u->region_sel < 1) u->region_sel++;
		else if (u->region_sel == 0 &&
		         (a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT)) {
			enum lang next = lang_get() == LANG_JA ? LANG_EN : LANG_JA;
			lang_set(next);
			settings_set(SETTINGS, LANG_KEY, lang_value(next));
			term_invalidate(&u->term);
		}
		else if (a == ACT_CONFIRM && u->region_sel == 1)
			open_tz(u);
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

	case SCR_DIAG:
		if (a == ACT_CONFIRM) run_portscope(u);
		else if (a == ACT_BACK) u->screen = SCR_SETTINGS;
		else if (a == ACT_QUIT) u->running = 0;
		break;

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
			/* Fetch the new channel's state immediately after switching. */
			const char *next = strcmp(u->upd.channel, "release") == 0
			                 ? "nightly" : "release";
			update_set_channel(SETTINGS, next);
			draw_busy(u, tr(S_CHECKING));
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
				draw_busy(u, tr(S_CHECKING));
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
				draw_busy(u, tr(S_BT_OFF));
				break;
			}
			/* Show the scan duration while the panel is blocked. */
			draw_busy(u, tr(S_SCANNING_8));
			bt_scan(&u->bt, 8);
			if (u->bt_sel > BT_HEAD + u->bt.n - 1)
				u->bt_sel = BT_HEAD;
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->bt_sel == 0) {
			draw_busy(u, tr(u->bt_on ? S_TURNING_OFF : S_TURNING_ON));
			bt_power(!u->bt_on);
			u->bt_on = bt_powered();
			memset(&u->bt, 0, sizeof(u->bt));
			if (u->bt_on)
				bt_list(&u->bt);
		}
		else if ((a == ACT_CONFIRM || a == ACT_LEFT || a == ACT_RIGHT) &&
		         u->bt_sel == 1) {
			u->bt_auto = !u->bt_auto;
			draw_busy(u, tr(S_APPLYING));
			bt_set_autoconnect(u->bt_auto);
			if (u->bt_on)
				bt_list(&u->bt);
		}
		else if (a == ACT_CONFIRM && sel >= 0 && sel < u->bt.n) {
			struct bt_device d = u->bt.d[sel];
			if (d.connected) {
				draw_busy(u, tr(S_DISCONNECTING));
				bt_disconnect(d.addr);
			} else {
				draw_busy(u, tr(S_CONNECTING));
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

static void wake_panel(struct ui *u)
{
	term_set_dim(&u->term, 0);
	kms_present(&u->kms);
	reapply_profile(u);
	term_invalidate(&u->term);
	status_read(&u->st);
	redraw(u);
	u->blanked = 0;
	u->last_act = now_ms();
	u->sleep_retry = 0;
}

/* power-handler returns once suspend is queued with the backlight off.
 * Restore the panel under that dark backlight so resume shows the menu.
 * Retry declined requests after a minute. */
static void idle_sleep(struct ui *u)
{
	disc_titles_stop(&u->discs);
	char *const argv[] = { (char *)POWER_HANDLER, (char *)"idle", NULL };
	if (proc_run_for(argv, NULL, NULL, 10000) != 0) {
		u->sleep_retry = now_ms() + IDLE_RETRY_MS;
		return;
	}
	wake_panel(u);
}

int main(void)
{
	struct ui u;
	memset(&u, 0, sizeof(u));

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);
	/* Power-handler requests panel blank/wake through SIGUSR1/SIGUSR2. */
	signal(SIGUSR1, on_signal);
	signal(SIGUSR2, on_signal);

	disc_titles_init(&u.discs, PS2_TITLES, PSX_TITLES, PSP_TITLES, NEOCD_TITLES, XBOX_TITLES, DISC_CACHE, DISC_HELPER);
	if (catalog_load_titles(&u.cat, ES_SYSTEMS, SETTINGS, &u.discs) < 0) {
		disc_titles_free(&u.discs);
		return 1;
	}
	recents_load(&u.recent, RECENT_FILE);
	favs_load(&u.favs, FAVS_FILE);

	if (kms_open(&u.kms, CARD) < 0) {
		disc_titles_free(&u.discs);
		catalog_free(&u.cat);
		return 1;
	}

	/* Let VRR lower refresh while the menu is static. */
	if (kms_vrr(&u.kms, 1) < 0)
		fprintf(stderr, "variable refresh is off\n");

	if (screen_init(&u) < 0) {
		fprintf(stderr, "term init failed\n");
		disc_titles_free(&u.discs);
		kms_close(&u.kms);
		catalog_free(&u.cat);
		return 1;
	}

	if (input_open(&u.in) < 0) {
		disc_titles_free(&u.discs);
		term_free(&u.term);
		kms_close(&u.kms);
		catalog_free(&u.cat);
		return 1;
	}

	/* Register the FIFO after input_open, which clears aux_fd. */
	if (notify_open(&u.notify) == 0)
		input_set_aux(&u.in, u.notify.fd);
	u.display_fd = uevent_open();
	if (u.display_fd >= 0)
		input_set_display(&u.in, u.display_fd);
	else
		fprintf(stderr, "display: no hotplug events, staying on this output\n");

	fprintf(stderr, "portarelauncher: %ux%u, %ux%u grid, %d systems\n",
	        u.kms.mode.hdisplay, u.kms.mode.vdisplay,
	        u.term.cols, u.term.rows, u.cat.n);

	/* Store palettes by name so reordering them preserves the user's choice. */
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
		if (!found)   /* unknown palette from an older build */
			u.pal_name = term_set_palette(&u.term, 0);
	}

	/* Apply before the first frame. Stock needs no write after boot. */
	{
		char prof[32] = "";
		u.profile_ok[0] = 1;
		/* Prefer <key>-igc.profile when DEGAMMA_LUT is supported;
		 * fall back to <key>.profile. */
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
		/* A missing or invalid screen-off time is the default, a missing or
		 * invalid sleep time the shortest offered. */
		u.blank_idx = BLANK_DEFAULT;
		char blk[8] = "";
		settings_get(SETTINGS, KEY_BLANK, blk, sizeof(blk));
		for (int i = 0; i < N_BLANK; i++) {
			char want[8];
			snprintf(want, sizeof(want), "%d", blank_minutes[i]);
			if (strcmp(blk, want) == 0)
				u.blank_idx = i;
		}
		char slp[8] = "";
		settings_get(SETTINGS, KEY_SLEEP, slp, sizeof(slp));
		for (int i = 0; i < N_SLEEP; i++) {
			char want[8];
			snprintf(want, sizeof(want), "%d", sleep_minutes[i]);
			if (strcmp(slp, want) == 0)
				u.sleep_idx = i;
		}
		for (int i = 1; i < N_PROFILES; i++)
			if (u.profile_ok[i] && strcmp(prof, profile_names[i].key) == 0 &&
			    kms_color_apply(&u.kms, &u.profiles[i]) == 0)
				u.profile = i;
	}

	/* Default to the Retroid labels printed on the device. */
	char style[32];
	/* Accept legacy "ps" and "sony" values as aliases for "shapes". */
	u.retroid = !(settings_get(SETTINGS, KEY_BUTTONS, style, sizeof(style)) &&
	              (strcmp(style, "shapes") == 0 || strcmp(style, "ps") == 0 ||
	               strcmp(style, "sony") == 0));
	input_set_layout(u.retroid);
	char lang[16] = "";
	settings_get(SETTINGS, LANG_KEY, lang, sizeof(lang));
	lang_set(lang_parse(lang));
	if (ja26_load(JA26_PATH) < 0)
		fprintf(stderr, "ja26: no %s, Unifont's glyphs it is\n", JA26_PATH);
	/* Defer the slow Wi-Fi scan until its screen opens. */
	tools_load(&u.tools);
	usb_modes(u.usb_opts, &u.n_usb, 8);
	usb_mode(u.usb, sizeof(u.usb));
	/* Build metadata changes only after reboot. */
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
	u.last_act = u.started;
	u.net_next = u.started + NET_POLL_FAST_MS;
	redraw(&u);

	while (u.running && !stop_requested) {
		if (disc_titles_step(&u.discs)) {
			for (int i = 0; i < u.cat.n; i++) {
				struct psystem *s = &u.cat.sys[i];
				if (!disc_titles_supported(s->name)) continue;
				char keep[512] = "";
				int visible = u.screen == SCR_GAMES && sys_index(&u) == i;
				if (visible && u.game_sel < s->ngames)
					str_copy(keep, sizeof(keep), s->games[u.game_sel].path);
				for (int k = 0; k < s->ngames; k++)
					if (!strcmp(s->games[k].path, u.discs.completed))
						disc_titles_apply(&u.discs, s->name, &s->games[k]);
				int selected = catalog_sort_games(s, keep);
				if (visible) u.game_sel = selected;
			}
			marquee_reset(&u);
			if (!u.blanked) redraw(&u);
		}
		/* Start with the minute boundary; pending work may shorten the wait. */
		int idle = status_ms_to_next_minute();
		if (u.panel_lost && idle > 1000)
			idle = 1000;
		int mq_left = marquee_left(&u);
		if (mq_left >= 0 && mq_left < idle)
			idle = mq_left;
		long long net_left = u.net_next - now_ms();
		if (net_left < idle)
			idle = net_left > 0 ? (int)net_left : 0;
		/* While blanked, wait only for input, signals or sleep.
		 * Keep panel recovery retries active if master is lost. */
		if (u.blanked && !u.panel_lost)
			idle = u.going_down ? -1
			     : idle_wait_ms(now_ms(), u.last_act, 1,
			                    blank_minutes[u.blank_idx],
			                    sleep_minutes[u.sleep_idx], u.sleep_retry);
		else if (!u.blanked) {
			int blank_left = idle_wait_ms(now_ms(), u.last_act, 0,
			                              blank_minutes[u.blank_idx], 0, 0);
			if (blank_left < idle)
				idle = blank_left;
			long long dim_left = idle_dim_at(u.last_act, blank_minutes[u.blank_idx]) - now_ms();
			if (!u.term.dim && dim_left < idle)
				idle = dim_left > 0 ? (int)dim_left : 0;
		}

		int metadata_left = proc_job_left(&u.discs.job);
		if (u.discs.queue && !u.discs.job.pid) metadata_left = 0;
		if (metadata_left >= 0 && (idle < 0 || metadata_left < idle)) idle = metadata_left;
		enum action a = input_wait_job(&u.in, idle, u.discs.job.fd, u.discs.job.done_fd);
		if (a == ACT_METADATA) continue;
		/* Without master the follow waits for reclaim_panel. A plug wakes
		 * a blanked menu, on whichever output it lands. */
		if (a == ACT_DISPLAY) {
			if (uevent_read_hotplug(u.display_fd) && !u.panel_lost &&
			    follow_display(&u)) {
				reapply_profile(&u);
				u.blanked = 0;
				u.last_act = now_ms();
				redraw(&u);
			}
			continue;
		}
		net_poll(&u);

		if (blank_requested) {
			int on = blank_requested > 0;
			blank_requested = 0;
			if (on) {
				wake_panel(&u);
			} else {
				kms_blank(&u.kms);
				u.blanked = 1;
			}
			continue;
		}

		/* Consume the wake press without a menu action. Releases are ACT_NONE. */
		/* Recover master before handling blanking, or the blank could stall retries. */
		if (u.panel_lost) {
			if (reclaim_panel(&u, 0) == 0)
				redraw(&u);
			if (a == ACT_TICK || u.panel_lost)
				continue;
		}

		if (u.blanked) {
			if (a == ACT_TICK && !u.going_down &&
			    now_ms() >= idle_sleep_at(u.last_act,
			                              blank_minutes[u.blank_idx],
			                              sleep_minutes[u.sleep_idx],
			                              u.sleep_retry))
				idle_sleep(&u);
			if (a == ACT_NONE || a == ACT_TICK)
				continue;
			wake_panel(&u);
			continue;
		}

		if (a != ACT_NONE && a != ACT_TICK)
			u.last_act = now_ms();

		/* Only blank while the launcher owns the CRTC. */
		if (!u.going_down &&
		    now_ms() - u.last_act >= blank_minutes[u.blank_idx] * 60000LL) {
			kms_blank(&u.kms);
			u.blanked = 1;
			continue;
		}

		/* Halfway there the whole menu dims, for the OLED panel. A press
		 * brings it back and acts as usual: the menu is still readable. */
		if (term_set_dim(&u.term, now_ms() >= idle_dim_at(u.last_act,
		                                                   blank_minutes[u.blank_idx])) &&
		    a == ACT_NONE) {
			redraw(&u);
			continue;
		}

		if (a == ACT_NONE)
			continue;
		on_action(&u, a);
		redraw(&u);
	}

	notify_close(&u.notify);
	if (u.display_fd >= 0)
		close(u.display_fd);
	disc_titles_free(&u.discs);
	input_close(&u.in);
	term_free(&u.term);
	kms_close(&u.kms);
	favs_free(&u.favs);
	catalog_free(&u.cat);
	return 0;
}
