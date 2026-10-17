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

#define KEY_AUTOCONNECT "bluetooth.autoconnect"

/* bluetoothctl --timeout delays cached queries and does not bound hangs
 * when BlueZ or a device is unavailable. Use proc_run_for deadlines;
 * reserve --timeout for discovery duration. */
#define BT_QUERY_MS 5000
#define BT_RADIO_MS 30000

static int hexpair(const char *s)
{
	return isxdigit((unsigned char)s[0]) && isxdigit((unsigned char)s[1]);
}

/* Find a MAC by shape: prompts change field positions, and device names
 * may contain spaces. */
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

/* bluetoothctl uses a dash-separated address as the name of unnamed devices. */
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

	/* Ignore [NEW]/[DEL] commentary mixed into stdout; parse only listings. */
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
		/* Hide unnamed scan results, but keep paired devices visible. */
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

	/* Merge facts from overlapping listings. */
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

	/* Four bulk queries avoid an info process per device. */
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

	/* Keep bluetoothctl alive during discovery; BlueZ stops scanning when
	 * the requesting client exits. */
	char *const argv[] = { (char *)BTCTL, (char *)"--timeout", secs,
	                       (char *)"scan", (char *)"on", NULL };
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

/* Check the saved setting first: with BlueZ stopped, bluetoothctl hangs
 * until the process deadline. Off is the common case. */
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
	/* Pair and trust are idempotent. Trust before connecting so this device
	 * can reconnect even if auto-connect is disabled for other paired devices. */
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

		/* One detached attempt for a device already switched on; an unavailable
		 * device can block for the connection timeout. */
		if (on && !l.d[i].connected) {
			char *const argv[] = { (char *)BTCTL, (char *)"connect",
			                       l.d[i].addr, NULL };
			proc_spawn(argv);
		}
	}
}
