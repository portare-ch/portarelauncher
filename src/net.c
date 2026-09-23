#include "net.h"
#include "proc.h"

#include <stdlib.h>
#include <string.h>

#define NMCLI "/usr/bin/nmcli"
#define USBGADGET "/usr/bin/usbgadget"

/* nmcli -t escapes ':' and '\' in values. Splits one terse line into fields,
 * unescaping as it goes. Returns how many it found. */
static int terse_split(const char *line, char out[][80], int max)
{
	int n = 0;
	size_t w = 0;
	if (max <= 0)
		return 0;
	out[0][0] = '\0';

	for (const char *p = line; *p; p++) {
		if (*p == '\\' && p[1]) {
			p++;
			if (w + 1 < 80)
				out[n][w++] = *p;
		} else if (*p == ':') {
			out[n][w] = '\0';
			if (++n >= max)
				return n;
			w = 0;
			out[n][0] = '\0';
		} else if (w + 1 < 80) {
			out[n][w++] = *p;
		}
	}
	out[n][w] = '\0';
	return n + 1;
}

/* ---- Wi-Fi ---------------------------------------------------------- */

static void cb_saved(char *line, void *ctx)
{
	struct net_list *l = ctx;
	char f[4][80];
	if (terse_split(line, f, 4) < 2)
		return;
	if (strcmp(f[1], "802-11-wireless") != 0)
		return;
	if (l->n >= NET_MAX || !f[0][0])
		return;

	struct net_entry *e = &l->e[l->n++];
	memset(e, 0, sizeof(*e));
	str_copy(e->name, sizeof(e->name), f[0]);
	e->saved = 1;
	e->signal = -1;
}

static void cb_visible(char *line, void *ctx)
{
	struct net_list *l = ctx;
	char f[4][80];
	if (terse_split(line, f, 4) < 3 || !f[1][0])
		return;

	int active = f[0][0] == '*';
	int sig = atoi(f[2]);

	/* Already listed as a saved profile: fill in what the scan knows. */
	for (int i = 0; i < l->n; i++) {
		if (strcmp(l->e[i].name, f[1]) == 0) {
			if (sig > l->e[i].signal)
				l->e[i].signal = sig;
			if (active)
				l->e[i].active = 1;
			return;
		}
	}

	if (l->n >= NET_MAX)
		return;
	struct net_entry *e = &l->e[l->n++];
	memset(e, 0, sizeof(*e));
	str_copy(e->name, sizeof(e->name), f[1]);
	e->signal = sig;
	e->active = active;
}

static int by_rank(const void *a, const void *b)
{
	const struct net_entry *x = a, *y = b;
	if (x->active != y->active) return y->active - x->active;
	if (x->saved  != y->saved)  return y->saved  - x->saved;
	return y->signal - x->signal;
}

void net_scan(struct net_list *l, int rescan)
{
	memset(l, 0, sizeof(*l));

	char *const saved[] = { (char *)NMCLI, (char *)"-t", (char *)"-f",
	                        (char *)"NAME,TYPE", (char *)"connection",
	                        (char *)"show", NULL };
	proc_run(saved, cb_saved, l);

	if (rescan) {
		char *const rs[] = { (char *)NMCLI, (char *)"device", (char *)"wifi",
		                     (char *)"rescan", NULL };
		proc_run(rs, NULL, NULL);
	}

	char *const vis[] = { (char *)NMCLI, (char *)"-t", (char *)"-f",
	                      (char *)"IN-USE,SSID,SIGNAL", (char *)"device",
	                      (char *)"wifi", (char *)"list", NULL };
	proc_run(vis, cb_visible, l);

	qsort(l->e, (size_t)l->n, sizeof(l->e[0]), by_rank);
}

static void cb_first(char *line, void *ctx)
{
	char *out = ctx;
	if (!out[0])
		str_copy(out, 80, line);
}

int net_wifi_enabled(void)
{
	char v[80] = "";
	char *const argv[] = { (char *)NMCLI, (char *)"-t", (char *)"radio",
	                       (char *)"wifi", NULL };
	proc_run(argv, cb_first, v);
	return strcmp(v, "enabled") == 0;
}

void net_wifi_set(int on)
{
	char *const argv[] = { (char *)NMCLI, (char *)"radio", (char *)"wifi",
	                       (char *)(on ? "on" : "off"), NULL };
	proc_run(argv, NULL, NULL);
}

int net_connect(const char *name)
{
	char *const argv[] = { (char *)NMCLI, (char *)"connection", (char *)"up",
	                       (char *)"id", (char *)name, NULL };
	return proc_run(argv, NULL, NULL);
}

int net_disconnect(void)
{
	char *const argv[] = { (char *)NMCLI, (char *)"radio", (char *)"wifi",
	                       (char *)"off", NULL };
	return proc_run(argv, NULL, NULL);
}

static void cb_ip4(char *line, void *ctx)
{
	char *out = ctx;
	/* "IP4.ADDRESS[1]:192.168.1.42/24" */
	char *colon = strchr(line, ':');
	if (!colon || out[0])
		return;
	char *slash = strchr(colon + 1, '/');
	if (slash)
		*slash = '\0';
	str_copy(out, 40, colon + 1);
}

void net_address(char *out, size_t osz)
{
	char v[40] = "";
	char *const argv[] = { (char *)NMCLI, (char *)"-t", (char *)"-f",
	                       (char *)"IP4.ADDRESS", (char *)"device",
	                       (char *)"show", NULL };
	proc_run(argv, cb_ip4, v);
	str_copy(out, osz, v);
}

/* ---- USB gadget ----------------------------------------------------- */

struct mode_ctx { char (*out)[24]; int *n; int max; };

static void cb_modes(char *line, void *ctx)
{
	struct mode_ctx *m = ctx;
	char *tok = strtok(line, " \t");
	while (tok && *m->n < m->max) {
		str_copy(m->out[(*m->n)++], 24, tok);
		tok = strtok(NULL, " \t");
	}
}

void usb_modes(char out[][24], int *n, int max)
{
	*n = 0;
	struct mode_ctx ctx = { out, n, max };
	char *const argv[] = { (char *)USBGADGET, (char *)"--options", NULL };
	proc_run(argv, cb_modes, &ctx);
}

void usb_mode(char *out, size_t osz)
{
	char v[80] = "";
	char *const argv[] = { (char *)USBGADGET, NULL };
	proc_run(argv, cb_first, v);
	str_copy(out, osz, v[0] ? v : "unknown");
}

int usb_set_mode(const char *mode)
{
	char *const argv[] = { (char *)USBGADGET, (char *)mode, NULL };
	return proc_run(argv, NULL, NULL);
}

void usb_address(char *out, size_t osz)
{
	char v[80] = "";
	char *const argv[] = { (char *)USBGADGET, (char *)"address", NULL };
	proc_run(argv, cb_first, v);
	str_copy(out, osz, v);
}
