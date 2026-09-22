/* portarelauncher - a KMS launcher for PortareOS.
 *
 * Owns the panel, lists what there is to play, hands the display to an
 * emulator and takes it back. See README.md for why it looks like this.
 */
#include "catalog.h"
#include "input.h"
#include "kms.h"
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

enum screen { SCR_SYSTEMS, SCR_GAMES, SCR_SETTINGS };

struct ui {
	struct term term;
	struct kms kms;
	struct input in;
	struct catalog cat;

	const char *pal_name;
	struct status st;
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

enum { SET_BUTTONS = 0, SET_WIFI, SET_BLUETOOTH, SET_BRIGHTNESS, SET_USB,
       N_SETTINGS };

static const char *const settings_labels[N_SETTINGS] = {
	"Button style",
	"Wi-Fi",
	"Bluetooth",
	"Brightness",
	"USB gadget mode",
};

static volatile sig_atomic_t stop_requested;

static void on_signal(int sig)
{
	(void)sig;
	stop_requested = 1;
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
	char right[32];

	term_clear(t);
	term_puts(t, 1, 0, crumb, ATTR_TEXT);

	if (u->st.capacity >= 0)
		snprintf(right, sizeof(right), "%s  %s %d%%", u->st.clock,
		         u->st.charging ? "CHG" : "BAT", u->st.capacity);
	else
		snprintf(right, sizeof(right), "%s", u->st.clock);
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

	snprintf(crumb, sizeof(crumb), "PortareOS  >  %s",
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

	draw_frame(u, "PortareOS  >  Settings");

	for (int i = 0; i < N_SETTINGS; i++) {
		const char *value = "not wired up";
		if (i == SET_BUTTONS)
			value = u->retroid ? "Retroid" : "PS";
		draw_row(u, (unsigned)(3 + i), i == u->set_sel,
		         settings_labels[i], value);
	}

	term_hline(t, 3 + N_SETTINGS + 1, G_HLINE, ATTR_DIM);
	if (u->set_sel == SET_BUTTONS)
		draw_face(u, 3 + N_SETTINGS + 3, u->retroid);
	else
		term_puts(t, 4, 3 + N_SETTINGS + 3, "Not implemented yet.", ATTR_DIM);

	{
		struct face f = face_of(u->retroid);
		char hint[64];
		snprintf(hint, sizeof(hint), "%c CHANGE   %c BACK", f.bottom, f.right);
		term_puts(t, 1, t->rows - 1, hint, ATTR_MID);
	}
}

static void redraw(struct ui *u)
{
	switch (u->screen) {
	case SCR_SYSTEMS:  draw_systems(u);  break;
	case SCR_GAMES:    draw_games(u);    break;
	case SCR_SETTINGS: draw_settings(u); break;
	}
	term_flush(&u->term);
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
	}
}

int main(void)
{
	struct ui u;
	memset(&u, 0, sizeof(u));

	signal(SIGINT, on_signal);
	signal(SIGTERM, on_signal);

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
	u.screen = SCR_SYSTEMS;
	u.running = 1;
	status_read(&u.st);
	redraw(&u);

	while (u.running && !stop_requested) {
		/* Sleep until the minute turns over, or until a button is
		 * pressed. Nothing else wakes this program. */
		enum action a = input_wait(&u.in, status_ms_to_next_minute());
		if (a == ACT_NONE)
			continue;
		on_action(&u, a);
		redraw(&u);
	}

	input_close(&u.in);
	term_free(&u.term);
	kms_close(&u.kms);
	catalog_free(&u.cat);
	return 0;
}
