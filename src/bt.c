#include "bt.h"
#include "proc.h"
#include "settings.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define BTCTL "/usr/bin/bluetoothctl"
#define BTSCRIPT "/usr/bin/portareos-bluetooth"

/* Same file as every other setting on the device. See settings.h. */
#define KEY_AUTOCONNECT "bluetooth.autoconnect"

/* bluetoothctl's non-interactive mode waits for org.bluez to appear on the
 * bus, so with the service stopped every call sits out its whole timeout.
 * Queries get a short one because by then the daemon is up and answering;
 * pairing and connecting get a long one because that is radio time, and a
 * device on the far side of a room genuinely takes ten seconds.
 *
 * Nothing here is called at all while the adapter is off. That is what
 * bt_enabled() below is for. */
#define BT_FAST "5"
#define BT_SLOW "20"

static int hexpair(const char *s)
{
	return isxdigit((unsigned char)s[0]) && isxdigit((unsigned char)s[1]);
}

/* Finds a MAC anywhere in the line and returns where it starts, or NULL.
 *
 * By position rather than by field number: bluetoothctl prefixes lines with
 * a coloured "[bluetooth]#" prompt in some builds and not in others, and a
 * device name can contain anything at all. The address is the one token in
 * the line whose shape is fixed. */
static char *find_mac(char *line)
{
	for (char *p = line; p[0] && p[1]; p++) {
		if (p != line && p[-1] != ' ' && p[-1] != '\t')
			continue;
		int ok = 1;
		for (int i = 0; i < 6 && ok; i++) {
			const char *g = p + i * 3;
			if (!hexpair(g))
				ok = 0;
			else if (i < 5 && g[2] != ':')
				ok = 0;
		}
		if (ok) {
			char after = p[17];
			if (after == '\0' || after == ' ' || after == '\t')
				return p;
		}
	}
	return NULL;
}

/* ---- listing ---------------------------------------------------------- */

struct add_ctx {
	struct bt_list *l;
	int paired, trusted, connected;   /* which list this is */
};

static struct bt_device *find_dev(struct bt_list *l, const char *addr)
{
	for (int i = 0; i < l->n; i++)
		if (strcasecmp(l->d[i].addr, addr) == 0)
			return &l->d[i];
	return NULL;
}

static void cb_device(char *line, void *ctx)
{
	struct add_ctx *a = ctx;
	strip_ansi(line);

	char *mac = find_mac(line);
	if (!mac)
		return;

	char addr[20];
	memcpy(addr, mac, 17);
	addr[17] = '\0';

	const char *name = mac + 17;
	while (*name == ' ' || *name == '\t')
		name++;

	struct bt_device *d = find_dev(a->l, addr);
	if (!d) {
		if (a->l->n >= BT_MAX)
			return;
		d = &a->l->d[a->l->n++];
		memset(d, 0, sizeof(*d));
		str_copy(d->addr, sizeof(d->addr), addr);
		str_copy(d->name, sizeof(d->name), *name ? name : addr);
	} else if (*name && strcasecmp(d->name, d->addr) == 0) {
		str_copy(d->name, sizeof(d->name), name);
	}

	/* The four listings overlap; each one only ever adds a fact. */
	d->paired    |= a->paired;
	d->trusted   |= a->trusted;
	d->connected |= a->connected;
}

static void list_kind(struct bt_list *l, const char *kind,
                      int paired, int trusted, int connected)
{
	struct add_ctx ctx = { l, paired, trusted, connected };
	char *const argv[] = { (char *)BTCTL, (char *)"--timeout",
	                       (char *)BT_FAST, (char *)"devices",
	                       (char *)kind, NULL };
	char *const argv_all[] = { (char *)BTCTL, (char *)"--timeout",
	                           (char *)BT_FAST, (char *)"devices", NULL };
	proc_run(kind ? argv : argv_all, cb_device, &ctx);
}

static int by_rank(const void *a, const void *b)
{
	const struct bt_device *x = a, *y = b;
	if (x->connected != y->connected) return y->connected - x->connected;
	if (x->paired    != y->paired)    return y->paired    - x->paired;
	return strcasecmp(x->name, y->name);
}

void bt_list(struct bt_list *l)
{
	memset(l, 0, sizeof(*l));

	/* Four calls whatever the number of devices, rather than one `info`
	 * per device. After a scan in a cafe that is the difference between
	 * four processes and forty. */
	list_kind(l, "Connected", 0, 0, 1);
	list_kind(l, "Paired",    1, 0, 0);
	list_kind(l, "Trusted",   0, 1, 0);
	list_kind(l, NULL,        0, 0, 0);

	qsort(l->d, (size_t)l->n, sizeof(l->d[0]), by_rank);
}

void bt_scan(struct bt_list *l, int seconds)
{
	char secs[8];
	if (seconds < 1 || seconds > 60)
		seconds = 8;
	snprintf(secs, sizeof(secs), "%d", seconds);

	/* --timeout is what makes this work at all. Without it bluetoothctl
	 * starts discovery, exits, and BlueZ stops discovering again because
	 * the client that asked for it is gone. */
	char *const argv[] = { (char *)BTCTL, (char *)"--timeout", secs,
	                       (char *)"scan", (char *)"on", NULL };
	proc_run(argv, NULL, NULL);

	bt_list(l);
}

/* ---- adapter ---------------------------------------------------------- */

static void cb_powered(char *line, void *ctx)
{
	int *on = ctx;
	strip_ansi(line);
	const char *p = strstr(line, "Powered:");
	if (p && strstr(p, "yes"))
		*on = 1;
}

/* What the OS thinks. Asked first because bluetoothctl blocks for its whole
 * timeout when the service is stopped, and "bluetooth is off" is the common
 * case on a device that is mostly used without headphones. */
static int bt_enabled(void)
{
	char v[8];
	return settings_get(SETTINGS_PATH, "controllers.bluetooth.enabled",
	                    v, sizeof(v)) && strcmp(v, "1") == 0;
}

int bt_powered(void)
{
	int on = 0;

	if (!bt_enabled())
		return 0;
	char *const argv[] = { (char *)BTCTL, (char *)"--timeout",
	                       (char *)BT_FAST, (char *)"show", NULL };
	proc_run(argv, cb_powered, &on);
	return on;
}

void bt_power(int on)
{
	char *const argv[] = { (char *)BTSCRIPT,
	                       (char *)(on ? "enable" : "disable"), NULL };
	proc_run(argv, NULL, NULL);
}

/* ---- connecting ------------------------------------------------------- */

static int btctl(const char *timeout, const char *cmd, const char *addr)
{
	char *const argv[] = { (char *)BTCTL, (char *)"--timeout",
	                       (char *)timeout, (char *)cmd, (char *)addr,
	                       NULL };
	return proc_run(argv, NULL, NULL);
}

int bt_connect(const char *addr)
{
	/* pair and trust are idempotent and both fail harmlessly on a device
	 * that is already either, so there is no state to check first. Trust
	 * before connect regardless of the auto-connect setting: without it
	 * the headphones cannot call back, and the setting governs what
	 * happens to the devices you are not touching right now. */
	btctl(BT_SLOW, "pair", addr);
	btctl(BT_FAST, "trust", addr);
	return btctl(BT_SLOW, "connect", addr);
}

int bt_disconnect(const char *addr)
{
	return btctl(BT_FAST, "disconnect", addr);
}

/* ---- auto-connect ----------------------------------------------------- */

int bt_autoconnect(void)
{
	char v[16];
	/* Default on. Somebody who has paired headphones wants them to come
	 * back, and the cost of being wrong is one toggle. */
	if (!settings_get(SETTINGS_PATH, KEY_AUTOCONNECT, v, sizeof(v)))
		return 1;
	return strcmp(v, "0") != 0 && strcasecmp(v, "no") != 0;
}

void bt_set_autoconnect(int on)
{
	settings_set(SETTINGS_PATH, KEY_AUTOCONNECT, on ? "1" : "0");

	struct bt_list l;
	memset(&l, 0, sizeof(l));
	list_kind(&l, "Paired", 1, 0, 0);
	list_kind(&l, "Connected", 0, 0, 1);

	for (int i = 0; i < l.n; i++) {
		if (!l.d[i].paired)
			continue;
		btctl(BT_FAST, on ? "trust" : "untrust", l.d[i].addr);

		/* One attempt from this side, detached, for a device that was
		 * already switched on and waiting. Detached because it blocks
		 * for the page timeout when the device is not there, and the
		 * panel is not allowed to stop for that. */
		if (on && !l.d[i].connected) {
			char *const argv[] = { (char *)BTCTL, (char *)"--timeout",
			                       (char *)BT_SLOW, (char *)"connect",
			                       l.d[i].addr, NULL };
			proc_spawn(argv);
		}
	}
}
