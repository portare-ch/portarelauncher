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

/* portnet prints one tab-separated line per network:
 *
 *   SSID \t known \t connected \t signal \t type
 *
 * which is both of the nmcli passes this used to make. "known" is iwd
 * having a passphrase for it, which is what "saved" meant here. A network
 * iwd knows but cannot see has no entry in the scan, so its signal comes
 * back -1, the same as a saved profile out of range did before. */
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
	/* iwd names the security by key type - psk, open, 8021x, wep - where
	 * NetworkManager named the protocol. net_min_password reads both. */
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

	/* One call. portnet waits for the scan to finish when asked to
	 * rescan, so there is nothing to sleep on here. */
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
	/* Both switches, and in this order. The boot turns Wi-Fi off with
	 * wifictl, which is an rfkill block, and powering the adapter does
	 * not lift that - so "on" through the adapter alone left the radio
	 * blocked. That was true of NetworkManager's switch and is true of
	 * iwd's: rfkill sits underneath both. */
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
	/* The password goes on portnet's command line, where anything running
	 * as root can read it for the second or two the join takes. That is
	 * accepted rather than engineered around: iwd stores the same
	 * passphrase in plain text under /var/lib/iwd the moment this
	 * succeeds, and everything on this device runs as root. What would
	 * not be acceptable is a long-lived process holding it there, which
	 * is the problem with the file server in portareos#239.
	 *
	 * Nothing has to be undone when this fails. iwd is told the
	 * passphrase through an agent and only writes it once the association
	 * succeeds, so a wrong one leaves no saved network behind - which is
	 * what the NetworkManager path needed an explicit delete for.
	 *
	 * portnet's exit codes are this function's: 0, 4 for a refused
	 * passphrase, 10 for a network that is no longer there. */
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
	/* iwd names these by key type rather than by protocol. 8021x wants a
	 * username as well, which this cannot ask for. */
	if (!strcmp(security, "8021x"))
		return -1;
	/* psk covers WPA and WPA2 personal, an 8 to 63 character passphrase,
	 * and WPA3 as well - SAE has no floor in the standard. WEP keys are 5
	 * or 13 characters. iwd refuses what is wrong for either, so 1 is
	 * enough here to stop an empty submit. */
	if (!strcmp(security, "psk"))
		return 8;
	return 1;
}

int net_disconnect(void)
{
	char *const argv[] = { (char *)PORTNET, (char *)"disconnect", NULL };
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
