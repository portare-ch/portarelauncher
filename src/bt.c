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

/* bluetoothctl's --timeout is a floor, not a ceiling: measured on the
 * device, `--timeout 5 devices` takes five seconds to print what it already
 * had in 14 ms. Four of those to build one list is twenty seconds of frozen
 * panel, so none of these calls passes it.
 *
 * What --timeout does not do is stop anything hanging. `connect` against a
 * device that is not there never returns; with the service stopped, every
 * call waits forever for org.bluez to appear on the bus. Both were measured.
 * The ceiling that actually holds is proc_run_for's.
 *
 * A query answers from bluetoothd's cache, so a second is already generous
 * and five is only there to bound the damage. Radio work gets thirty. */
#define BT_QUERY_MS 5000
#define BT_RADIO_MS 30000

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

/* An unnamed device is listed under its own address with dashes for colons:
 *
 *     Device C0:28:8D:87:51:82 C0-28-8D-87-51-82
 *
 * Which is every BLE beacon within thirty metres. Twelve of them turned up
 * in one eight-second scan on a quiet evening at home, and a wall of hex is
 * not a list somebody finds their headphones in. */
static int name_is_address(const char *name, const char *addr)
{
	for (int i = 0; i < 17; i++) {
		char n = name[i], d = addr[i];
		if (n == '-')
			n = ':';
		if (n != d)
			return 0;
	}
	return name[17] == '\0';
}

static void cb_device(char *line, void *ctx)
{
	struct add_ctx *a = ctx;
	strip_ansi(line);

	/* Only the listing, never the running commentary. bluetoothctl writes
	 * `[NEW] Device ...` and `[DEL] Device ...` to the same stdout as it
	 * goes, and those lines carry an address that would otherwise be
	 * parsed as a device this command was asked about. */
	if (strncmp(line, "Device ", 7) != 0)
		return;

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
		/* Nameless and not one of ours: noise. Something already
		 * paired is kept whatever it calls itself, because the user
		 * has to be able to see what they paired. */
		if (!a->paired && !a->trusted && !a->connected &&
		    (!*name || name_is_address(name, addr)))
			return;
		if (a->l->n >= BT_MAX)
			return;
		d = &a->l->d[a->l->n++];
		memset(d, 0, sizeof(*d));
		str_copy(d->addr, sizeof(d->addr), addr);
		str_copy(d->name, sizeof(d->name), *name ? name : addr);
	} else if (*name && !name_is_address(name, addr) &&
	           name_is_address(d->name, d->addr)) {
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
	char *const argv[] = { (char *)BTCTL, (char *)"devices",
	                       (char *)kind, NULL };
	char *const argv_all[] = { (char *)BTCTL, (char *)"devices", NULL };
	proc_run_for(kind ? argv : argv_all, cb_device, &ctx, BT_QUERY_MS);
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
	/* The one place --timeout is the right tool: here it is how long to
	 * listen, which is the whole point. The ceiling sits above it. */
	proc_run_for(argv, NULL, NULL, seconds * 1000 + BT_QUERY_MS);

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

/* What the OS thinks. Asked first because with the service stopped
 * bluetoothctl waits forever for org.bluez to appear on the bus - it is the
 * ceiling that ends the call, not bluetoothctl - and "bluetooth is off" is
 * the common case on a handheld used without headphones. Reading a line out
 * of a file beats burning the ceiling to learn the same thing. */
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
	char *const argv[] = { (char *)BTCTL, (char *)"show", NULL };
	proc_run_for(argv, cb_powered, &on, BT_QUERY_MS);
	return on;
}

void bt_power(int on)
{
	char *const argv[] = { (char *)BTSCRIPT,
	                       (char *)(on ? "enable" : "disable"), NULL };
	proc_run(argv, NULL, NULL);
}

/* ---- connecting ------------------------------------------------------- */

static int btctl(const char *cmd, const char *addr, int ceiling_ms)
{
	char *const argv[] = { (char *)BTCTL, (char *)cmd, (char *)addr, NULL };
	return proc_run_for(argv, NULL, NULL, ceiling_ms);
}

int bt_connect(const char *addr)
{
	/* pair and trust are idempotent and both fail harmlessly on a device
	 * that is already either, so there is no state to check first. Trust
	 * before connect regardless of the auto-connect setting: without it
	 * the headphones cannot call back, and the setting governs what
	 * happens to the devices you are not touching right now. */
	btctl("pair", addr, BT_RADIO_MS);
	btctl("trust", addr, BT_QUERY_MS);
	return btctl("connect", addr, BT_RADIO_MS);
}

int bt_disconnect(const char *addr)
{
	return btctl("disconnect", addr, BT_QUERY_MS);
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
		btctl(on ? "trust" : "untrust", l.d[i].addr, BT_QUERY_MS);

		/* One attempt from this side, detached, for a device that was
		 * already switched on and waiting. Detached because it blocks
		 * for the page timeout when the device is not there, and the
		 * panel is not allowed to stop for that. */
		if (on && !l.d[i].connected) {
			char *const argv[] = { (char *)BTCTL, (char *)"connect",
			                       l.d[i].addr, NULL };
			proc_spawn(argv);
		}
	}
}
