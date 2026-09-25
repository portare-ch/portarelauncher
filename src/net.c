#include "net.h"
#include "proc.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#define NMCLI "/usr/bin/nmcli"
#define USBGADGET "/usr/bin/usbgadget"
#define WIFICTL "/usr/bin/wifictl"
#define SYSTEMCTL "/usr/bin/systemctl"
#define SSHD_CONF "/storage/.cache/services/sshd.conf"

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
	char f[4][80] = { { 0 } };   /* a line without SECURITY leaves f[3] */
	if (terse_split(line, f, 4) < 3 || !f[1][0])
		return;

	int active = f[0][0] == '*';
	int sig = atoi(f[2]);
	/* Open networks report "" in terse mode and "--" in the tabular one;
	 * accept both rather than depend on which this nmcli does. */
	const char *sec = strcmp(f[3], "--") == 0 ? "" : f[3];

	/* Already listed as a saved profile: fill in what the scan knows. */
	for (int i = 0; i < l->n; i++) {
		if (strcmp(l->e[i].name, f[1]) == 0) {
			if (sig > l->e[i].signal)
				l->e[i].signal = sig;
			if (active)
				l->e[i].active = 1;
			str_copy(l->e[i].security, sizeof(l->e[i].security), sec);
			return;
		}
	}

	if (l->n >= NET_MAX)
		return;
	struct net_entry *e = &l->e[l->n++];
	memset(e, 0, sizeof(*e));
	str_copy(e->name, sizeof(e->name), f[1]);
	str_copy(e->security, sizeof(e->security), sec);
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
	                      (char *)"IN-USE,SSID,SIGNAL,SECURITY", (char *)"device",
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
	/* Both switches. The boot turns Wi-Fi off with wifictl, which is an
	 * rfkill block, and NetworkManager's own radio switch does not lift
	 * that - so "on" through nmcli alone left the radio blocked. */
	char *const rf[] = { (char *)WIFICTL, (char *)(on ? "enable" : "disable"),
	                     NULL };
	char *const nm[] = { (char *)NMCLI, (char *)"radio", (char *)"wifi",
	                     (char *)(on ? "on" : "off"), NULL };
	proc_run(rf, NULL, NULL);
	proc_run(nm, NULL, NULL);
}

/* ---- SSH ------------------------------------------------------------ */

int net_ssh_enabled(void)
{
	char v[80] = "";
	char *const argv[] = { (char *)SYSTEMCTL, (char *)"is-active",
	                       (char *)"sshd", NULL };
	proc_run(argv, cb_first, v);
	return strcmp(v, "active") == 0;
}

void net_ssh_set(int on)
{
	char *const touch[] = { (char *)"/usr/bin/touch", (char *)SSHD_CONF, NULL };
	char *const rm[] = { (char *)"/usr/bin/rm", (char *)"-f", (char *)SSHD_CONF,
	                     NULL };
	char *const sc[] = { (char *)SYSTEMCTL, (char *)(on ? "start" : "stop"),
	                     (char *)"sshd", NULL };
	if (on) {
		proc_run(touch, NULL, NULL);
		proc_run(sc, NULL, NULL);
	} else {
		proc_run(sc, NULL, NULL);
		proc_run(rm, NULL, NULL);
	}
}

int net_connect(const char *name)
{
	char *const argv[] = { (char *)NMCLI, (char *)"connection", (char *)"up",
	                       (char *)"id", (char *)name, NULL };
	return proc_run(argv, NULL, NULL);
}

int net_join(const char *ssid, const char *password)
{
	/* The password goes on nmcli's command line, where anything running
	 * as root can read it for the second or two nmcli takes. That is
	 * accepted rather than engineered around: NetworkManager stores the
	 * same password in plain text in its profile the moment this
	 * succeeds, and everything on this device runs as root. What would
	 * not be acceptable is a long-lived process holding it there, which
	 * is the problem with the file server in portareos#239.
	 *
	 * nmcli's own --wait comes in under our ceiling, so a slow join is
	 * reported by nmcli as a timeout rather than killed by us. */
	char *const argv_pw[] = { (char *)NMCLI, (char *)"--wait", (char *)"30",
	                          (char *)"device", (char *)"wifi", (char *)"connect",
	                          (char *)ssid, (char *)"password",
	                          (char *)password, NULL };
	char *const argv_open[] = { (char *)NMCLI, (char *)"--wait", (char *)"30",
	                            (char *)"device", (char *)"wifi",
	                            (char *)"connect", (char *)ssid, NULL };
	int rc = proc_run_for(password && password[0] ? argv_pw : argv_open,
	                      NULL, NULL, 40000);

	if (rc != 0) {
		/* Only ever called for a network with no saved profile, so a
		 * profile by this name now is the one this attempt created. */
		char *const del[] = { (char *)NMCLI, (char *)"connection",
		                      (char *)"delete", (char *)"id", (char *)ssid,
		                      NULL };
		proc_run(del, NULL, NULL);
	}
	return rc;
}

int net_min_password(const char *security)
{
	if (!security || !security[0])
		return 0;
	if (strstr(security, "802.1X"))
		return -1;
	/* WPA and WPA2 personal: an 8 to 63 character passphrase. WPA3's SAE
	 * has no such floor in the standard, and WEP keys are 5 or 13
	 * characters; nmcli rejects what is wrong for either, so 1 is enough
	 * to stop an empty submit. */
	if (strstr(security, "WPA1") || strstr(security, "WPA2"))
		return 8;
	return 1;
}

int net_disconnect(void)
{
	char *const argv[] = { (char *)NMCLI, (char *)"radio", (char *)"wifi",
	                       (char *)"off", NULL };
	return proc_run(argv, NULL, NULL);
}

/* Wi-Fi first, then whatever else is up: a docked device with Ethernet and
 * Wi-Fi shows the one a person would type. Loopback and the USB gadget's
 * link-local subnet are not addresses anyone reaches the device on. */
static int addr_rank(const char *ifname, const struct sockaddr_in *sa)
{
	unsigned a = ntohl(sa->sin_addr.s_addr);
	if ((a >> 24) == 127 || (a >> 16) == 0xA9FE)   /* 127/8, 169.254/16 */
		return 0;
	if (strncmp(ifname, "wlan", 4) == 0)
		return 3;
	if (strncmp(ifname, "usb", 3) == 0 || strncmp(ifname, "rndis", 5) == 0 ||
	    strcmp(ifname, "gadget") == 0)
		return 1;
	return 2;
}

int net_address_pick(const struct ifaddrs *list, char *out, size_t osz)
{
	int best = 0;
	out[0] = '\0';
	for (const struct ifaddrs *i = list; i; i = i->ifa_next) {
		if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET ||
		    !(i->ifa_flags & IFF_UP) || !i->ifa_name)
			continue;
		const struct sockaddr_in *sa = (const struct sockaddr_in *)i->ifa_addr;
		int rank = addr_rank(i->ifa_name, sa);
		if (rank > best) {
			best = rank;
			inet_ntop(AF_INET, &sa->sin_addr, out, (socklen_t)osz);
		}
	}
	return best > 0;
}

void net_address(char *out, size_t osz)
{
	/* The kernel's own list, not nmcli: this is asked every few seconds
	 * while the launcher waits for the network to come up, and a process
	 * per ask would be felt. */
	struct ifaddrs *list = NULL;
	if (getifaddrs(&list) < 0) {
		out[0] = '\0';
		return;
	}
	net_address_pick(list, out, osz);
	freeifaddrs(list);
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
