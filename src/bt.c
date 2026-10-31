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

static int run(const struct bt_client *c, char *const argv[],
               void (*cb)(char *, void *), void *ctx, int ceiling_ms)
{
	return c && c->run ? c->run(c->ctx, argv, cb, ctx, ceiling_ms)
	                   : proc_run_for(argv, cb, ctx, ceiling_ms);
}

static int list_kind(const struct bt_client *c, struct bt_list *l, const char *kind,
                      int paired, int trusted, int connected)
{
	struct add_ctx ctx = { l, paired, trusted, connected };
	char *const argv[] = { (char *)BTCTL, (char *)"devices",
	                       (char *)kind, NULL };
	char *const argv_all[] = { (char *)BTCTL, (char *)"devices", NULL };
	return run(c, kind ? argv : argv_all, cb_device, &ctx, BT_QUERY_MS);
}

static int by_rank(const void *a, const void *b)
{
	const struct bt_device *x = a, *y = b;
	if (x->connected != y->connected) return y->connected - x->connected;
	if (x->paired    != y->paired)    return y->paired    - x->paired;
	return strcasecmp(x->name, y->name);
}

int bt_list(const struct bt_client *c, struct bt_list *l)
{
	struct bt_list next = {0};
	int rc;
	if ((rc = list_kind(c, &next, "Connected", 0, 0, 1)) ||
	    (rc = list_kind(c, &next, "Paired",    1, 0, 0)) ||
	    (rc = list_kind(c, &next, "Trusted",   0, 1, 0)) ||
	    (rc = list_kind(c, &next, NULL,        0, 0, 0)))
		return rc;
	qsort(next.d, (size_t)next.n, sizeof(next.d[0]), by_rank);
	*l = next;
	return 0;
}

int bt_scan(const struct bt_client *c, struct bt_list *l, int seconds)
{
	char secs[8];
	if (seconds < 1 || seconds > 60)
		seconds = 8;
	snprintf(secs, sizeof(secs), "%d", seconds);

	/* BlueZ stops discovery when the requesting client exits. */
	char *const argv[] = { (char *)BTCTL, (char *)"--timeout", secs,
	                       (char *)"scan", (char *)"on", NULL };
	int rc = run(c, argv, NULL, NULL, seconds * 1000 + BT_QUERY_MS);
	return rc ? rc : bt_list(c, l);
}

/* ---- adapter ---------------------------------------------------------- */

static void cb_powered(char *line, void *ctx)
{
	int *on = ctx;
	strip_ansi(line);
	while (isspace((unsigned char)*line)) line++;
	const char *p = line;
	if (!strncmp(p, "Powered:", strlen("Powered:"))) {
		p += strlen("Powered:");
		while (isspace((unsigned char)*p)) p++;
		if (!strcmp(p, "yes")) *on = 1;
		else if (!strcmp(p, "no")) *on = 0;
	}
}

/* Check the saved setting first: with BlueZ stopped, bluetoothctl hangs
 * until the process deadline. Off is the common case. */
static int bt_enabled(void)
{
	char v[8];
	return settings_get(SETTINGS_PATH, "controllers.bluetooth.enabled",
	                    v, sizeof(v)) && strcmp(v, "1") == 0;
}

int bt_powered(const struct bt_client *c)
{
	int on = -1;

	if (!bt_enabled())
		return 0;
	char *const argv[] = { (char *)BTCTL, (char *)"show", NULL };
	int rc = run(c, argv, cb_powered, &on, BT_QUERY_MS);
	if (rc) return rc < 0 ? rc : PROC_FAILED;
	return on;
}

int bt_power(const struct bt_client *c, int on)
{
	char *const argv[] = { (char *)BTSCRIPT,
	                       (char *)(on ? "enable" : "disable"), NULL };
	return run(c, argv, NULL, NULL, PROC_CEILING_MS);
}

/* ---- connecting ------------------------------------------------------- */

static int btctl(const struct bt_client *c, const char *cmd, const char *addr,
                 int ceiling_ms)
{
	char *const argv[] = { (char *)BTCTL, (char *)cmd, (char *)addr, NULL };
	return run(c, argv, NULL, NULL, ceiling_ms);
}

struct connection { int paired, trusted, connected; };

static void cb_info(char *line, void *ctx)
{
	struct connection *d = ctx;
	strip_ansi(line);
	while (isspace((unsigned char)*line)) line++;
	const char *keys[] = { "Paired:", "Trusted:", "Connected:" };
	int *values[] = { &d->paired, &d->trusted, &d->connected };
	for (int i = 0; i < 3; i++) {
		size_t n = strlen(keys[i]);
		if (strncmp(line, keys[i], n)) continue;
		const char *v = line + n;
		while (isspace((unsigned char)*v)) v++;
		if (!strcmp(v, "yes")) *values[i] = 1;
		else if (!strcmp(v, "no")) *values[i] = 0;
	}
}

int bt_connect(const struct bt_client *c, const char *addr)
{
	/* Recheck the device: a headset may reconnect while the menu is open. */
	struct connection d = { -1, -1, -1 };
	char *const argv[] = { (char *)BTCTL, (char *)"info", (char *)addr, NULL };
	int rc = run(c, argv, cb_info, &d, BT_QUERY_MS);
	if (rc) return rc;
	if (d.paired < 0 || d.trusted < 0 || d.connected < 0) return PROC_FAILED;
	if (d.connected) return 0;
	if (!d.paired && (rc = btctl(c, "pair", addr, BT_RADIO_MS))) return rc;
	if (!d.trusted && (rc = btctl(c, "trust", addr, BT_QUERY_MS))) return rc;
	return btctl(c, "connect", addr, BT_RADIO_MS);
}

int bt_disconnect(const struct bt_client *c, const char *addr)
{
	return btctl(c, "disconnect", addr, BT_QUERY_MS);
}

/* ---- auto-connect ----------------------------------------------------- */

int bt_autoconnect(void)
{
	char v[16];
	if (!settings_get(SETTINGS_PATH, KEY_AUTOCONNECT, v, sizeof(v)))
		return 1;
	return strcmp(v, "0") != 0 && strcasecmp(v, "no") != 0;
}

int bt_set_autoconnect(const struct bt_client *c, int on)
{
	if (settings_set(SETTINGS_PATH, KEY_AUTOCONNECT, on ? "1" : "0"))
		return PROC_FAILED;

	struct bt_list l;
	memset(&l, 0, sizeof(l));
	int rc = list_kind(c, &l, "Paired", 1, 0, 0);
	if (!rc) rc = list_kind(c, &l, "Connected", 0, 0, 1);
	if (rc) return rc;

	for (int i = 0; i < l.n; i++) {
		if (!l.d[i].paired)
			continue;
		rc = btctl(c, on ? "trust" : "untrust", l.d[i].addr, BT_QUERY_MS);
		if (rc) return rc;

		/* One detached attempt for a device already switched on; an unavailable
		 * device can block for the connection timeout. */
		if (on && !l.d[i].connected) {
			char *const argv[] = { (char *)BTCTL, (char *)"connect",
			                       l.d[i].addr, NULL };
			proc_spawn(argv);
		}
	}
	return 0;
}
