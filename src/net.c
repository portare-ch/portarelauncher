#include "net.h"
#include "proc.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>

#define PORTNET "/usr/bin/portnet"
#define USBGADGET "/usr/bin/usbgadget"
#define WIFICTL "/usr/bin/wifictl"
#define SYSTEMCTL "/usr/bin/systemctl"
#define SSHD_CONF "/storage/.cache/services/sshd.conf"

/* ---- Wi-Fi ---------------------------------------------------------- */

/* portnet output: SSID \t known \t connected \t signal \t type.
 * Known networks outside scan range report signal -1. */
static void cb_net(char *line, void *ctx)
{
	struct net_list *l = ctx;
	char *f[5] = { line, NULL, NULL, NULL, NULL };
	int n = 1;
	for (char *p = line; *p && n < 5; p++)
		if (*p == '\t') {
			*p = '\0';
			f[n++] = p + 1;
		}
	if (n < 5 || !f[0][0] || l->n >= NET_MAX)
		return;

	struct net_entry *e = &l->e[l->n++];
	memset(e, 0, sizeof(*e));
	str_copy(e->name, sizeof(e->name), f[0]);
	e->saved  = atoi(f[1]);
	e->active = atoi(f[2]);
	e->signal = atoi(f[3]);
	/* portnet reports iwd key types rather than WPA protocol names. */
	str_copy(e->security, sizeof(e->security), strcmp(f[4], "open") ? f[4] : "");
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

	/* portnet waits for a requested rescan to finish. */
	char *const scan[]  = { (char *)PORTNET, (char *)"list", (char *)"--rescan", NULL };
	char *const cache[] = { (char *)PORTNET, (char *)"list", NULL };
	proc_run_for(rescan ? scan : cache, cb_net, l, 20000);

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
	char *const argv[] = { (char *)PORTNET, (char *)"radio", NULL };
	proc_run(argv, cb_first, v);
	return strcmp(v, "on") == 0;
}

void net_wifi_set(int on)
{
	/* Lift the boot-time rfkill block before powering the iwd adapter;
	 * adapter power alone cannot unblock the radio. */
	char *const rf[] = { (char *)WIFICTL, (char *)(on ? "enable" : "disable"),
	                     NULL };
	char *const radio[] = { (char *)PORTNET, (char *)"radio",
	                        (char *)(on ? "on" : "off"), NULL };
	proc_run(rf, NULL, NULL);
	proc_run(radio, NULL, NULL);
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
	char *const argv[] = { (char *)PORTNET, (char *)"connect", (char *)name,
	                       NULL };
	return proc_run_for(argv, NULL, NULL, 40000);
}

int net_join(const char *ssid, const char *password)
{
	/* The password is briefly visible in portnet's argv to root processes;
	 * iwd also stores it in plaintext after association succeeds. Failed joins
	 * leave no saved network. Return portnet's status (4 rejected, 10 gone). */
	char *const argv_pw[] = { (char *)PORTNET, (char *)"join", (char *)ssid,
	                          (char *)password, NULL };
	char *const argv_open[] = { (char *)PORTNET, (char *)"connect",
	                            (char *)ssid, NULL };
	return proc_run_for(password && password[0] ? argv_pw : argv_open,
	                    NULL, NULL, 40000);
}

int net_min_password(const char *security)
{
	if (!security || !security[0])
		return 0;
	/* 802.1X also requires a username, which this UI cannot request. */
	if (!strcmp(security, "8021x"))
		return -1;
	/* Require eight characters for PSK; leave other key validation to iwd. */
	if (!strcmp(security, "psk"))
		return 8;
	return 1;
}

int net_disconnect(void)
{
	char *const argv[] = { (char *)PORTNET, (char *)"disconnect", NULL };
	return proc_run(argv, NULL, NULL);
}

/* Prefer Wi-Fi over other active interfaces; exclude loopback and link-local. */
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
	/* Frequent startup polling uses getifaddrs to avoid spawning a process. */
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
