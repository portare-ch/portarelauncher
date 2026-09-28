#include "check.h"
#include "fake_proc.h"
#include "net.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#define LIST "/usr/bin/portnet list"
#define SCAN "/usr/bin/portnet list --rescan"

/* The shape of portnet's own output, names replaced: one tab-separated
 * line per network, SSID first, then known, connected, signal and the key
 * type. A network iwd knows but cannot see reports -1 for signal. */
static void test_scan(void)
{
	fake_reset();
	fake_reply(LIST,
		"Home\t1\t1\t59\tpsk\n"
		"Cafe:Guest\t1\t0\t40\topen\n"
		"Neighbour\t0\t0\t45\tpsk\n"
		"Open Net\t0\t0\t37\topen\n"
		"Far Away\t0\t0\t12\tpsk\n", 0);

	struct net_list l;
	net_scan(&l, 0);
	CHECK(!fake_called(SCAN));

	/* Connected, then known, then the rest by signal. */
	CHECK_INT(l.n, 5);
	if (l.n == 5) {
		CHECK_STR(l.e[0].name, "Home");
		CHECK(l.e[0].active && l.e[0].saved);
		CHECK_INT(l.e[0].signal, 59);
		CHECK_STR(l.e[0].security, "psk");

		/* An SSID with a colon needs no unescaping now: the fields are
		 * split on tabs, which an SSID cannot contain. */
		CHECK_STR(l.e[1].name, "Cafe:Guest");
		CHECK(l.e[1].saved && !l.e[1].active);
		CHECK_INT(l.e[1].signal, 40);
		CHECK_STR(l.e[1].security, "");

		CHECK_STR(l.e[2].name, "Neighbour");
		CHECK(!l.e[2].saved);
		CHECK_STR(l.e[3].name, "Open Net");
		CHECK_STR(l.e[4].name, "Far Away");
		CHECK_STR(l.e[4].security, "psk");
	}

	fake_reset();
	fake_reply(SCAN, "", 0);
	net_scan(&l, 1);
	CHECK(fake_called(SCAN));
	CHECK_INT(l.n, 0);
}

static void test_saved_out_of_range(void)
{
	fake_reset();
	fake_reply(LIST, "Holiday Flat\t1\t0\t-1\tpsk\n", 0);

	struct net_list l;
	net_scan(&l, 0);
	CHECK_INT(l.n, 1);
	CHECK(l.e[0].saved);
	CHECK_INT(l.e[0].signal, -1);
}

static void test_min_password(void)
{
	CHECK_INT(net_min_password(NULL), 0);
	CHECK_INT(net_min_password(""), 0);
	/* iwd names the key type, not the protocol. psk covers WPA, WPA2 and
	 * WPA3; wep has its own shorter keys; 8021x needs a username too. */
	CHECK_INT(net_min_password("psk"), 8);
	CHECK_INT(net_min_password("wep"), 1);
	CHECK_INT(net_min_password("8021x"), -1);
}

static void test_join(void)
{
	/* A refused passphrase is portnet's 4, and nothing has to be undone:
	 * iwd writes credentials only once the association succeeds, so there
	 * is no half-saved network to delete. */
	fake_reset();
	fake_reply("/usr/bin/portnet join Neighbour hunter22", "", 4);
	CHECK_INT(net_join("Neighbour", "hunter22"), 4);
	CHECK_INT(fake_n_calls, 1);

	/* Success. */
	fake_reset();
	fake_reply("/usr/bin/portnet join Neighbour hunter22", "", 0);
	CHECK_INT(net_join("Neighbour", "hunter22"), 0);
	CHECK_INT(fake_n_calls, 1);

	/* A network that has gone is 10, which the caller shows differently
	 * from a wrong password. */
	fake_reset();
	fake_reply("/usr/bin/portnet join Neighbour hunter22", "", 10);
	CHECK_INT(net_join("Neighbour", "hunter22"), 10);

	/* An open network is a connect, with no passphrase argument. */
	fake_reset();
	fake_reply("/usr/bin/portnet connect Open Net", "", 0);
	CHECK_INT(net_join("Open Net", ""), 0);
	CHECK_INT(fake_n_calls, 1);
}

static struct ifaddrs *add_if(struct ifaddrs *next, const char *name,
                              const char *ip, unsigned flags)
{
	struct ifaddrs *i = calloc(1, sizeof(*i));
	struct sockaddr_in *sa = calloc(1, sizeof(*sa));
	sa->sin_family = AF_INET;
	inet_pton(AF_INET, ip, &sa->sin_addr);
	i->ifa_next = next;
	i->ifa_name = (char *)name;
	i->ifa_flags = flags;
	i->ifa_addr = (struct sockaddr *)sa;
	return i;
}

static void free_ifs(struct ifaddrs *i)
{
	while (i) {
		struct ifaddrs *n = i->ifa_next;
		free(i->ifa_addr);
		free(i);
		i = n;
	}
}

static void test_address(void)
{
	char ip[40];

	/* Loopback and the gadget are never the answer; Wi-Fi beats Ethernet
	 * whatever the order. */
	struct ifaddrs *l = add_if(NULL, "lo", "127.0.0.1", IFF_UP);
	l = add_if(l, "usb0", "169.254.7.7", IFF_UP);
	l = add_if(l, "eth0", "10.0.0.5", IFF_UP);
	l = add_if(l, "wlan0", "192.168.178.81", IFF_UP);
	CHECK_INT(net_address_pick(l, ip, sizeof(ip)), 1);
	CHECK_STR(ip, "192.168.178.81");
	free_ifs(l);

	/* Ethernet alone. */
	l = add_if(NULL, "eth0", "10.0.0.5", IFF_UP);
	l = add_if(l, "lo", "127.0.0.1", IFF_UP);
	CHECK_INT(net_address_pick(l, ip, sizeof(ip)), 1);
	CHECK_STR(ip, "10.0.0.5");
	free_ifs(l);

	/* Wi-Fi with an address but the link down does not count. */
	l = add_if(NULL, "wlan0", "192.168.178.81", 0);
	l = add_if(l, "lo", "127.0.0.1", IFF_UP);
	CHECK_INT(net_address_pick(l, ip, sizeof(ip)), 0);
	CHECK_STR(ip, "");
	free_ifs(l);

	CHECK_INT(net_address_pick(NULL, ip, sizeof(ip)), 0);
}

static void test_usb(void)
{
	char modes[8][24];
	int n = 0;
	fake_reset();
	fake_reply("/usr/bin/usbgadget --options", "disabled network file_transfer\n", 0);
	usb_modes(modes, &n, 8);
	CHECK_INT(n, 3);
	if (n == 3) {
		CHECK_STR(modes[0], "disabled");
		CHECK_STR(modes[2], "file_transfer");
	}

	/* Never more than asked for. */
	usb_modes(modes, &n, 2);
	CHECK_INT(n, 2);
}

static void test_wifi_switch(void)
{
	/* Both switches, the rfkill block first: the boot's wifictl disable is
	 * what an official image starts with, and powering the adapter does
	 * not lift it. That was true of NetworkManager and is true of iwd. */
	fake_reset();
	fake_reply("/usr/bin/wifictl enable", "", 0);
	fake_reply("/usr/bin/portnet radio on", "", 0);
	net_wifi_set(1);
	CHECK_INT(fake_n_calls, 2);
	CHECK_STR(fake_calls[0], "/usr/bin/wifictl enable");
	CHECK_STR(fake_calls[1], "/usr/bin/portnet radio on");

	fake_reset();
	net_wifi_set(0);
	CHECK_STR(fake_calls[0], "/usr/bin/wifictl disable");
	CHECK_STR(fake_calls[1], "/usr/bin/portnet radio off");

	fake_reset();
	fake_reply("/usr/bin/portnet radio", "on\n", 0);
	CHECK(net_wifi_enabled());
	fake_reset();
	fake_reply("/usr/bin/portnet radio", "off\n", 0);
	CHECK(!net_wifi_enabled());
}

static void test_ssh_switch(void)
{
	/* The marker first, or systemd skips the start on its condition. */
	fake_reset();
	fake_reply("/usr/bin/touch /storage/.cache/services/sshd.conf", "", 0);
	fake_reply("/usr/bin/systemctl start sshd", "", 0);
	net_ssh_set(1);
	CHECK_INT(fake_n_calls, 2);
	CHECK_STR(fake_calls[0], "/usr/bin/touch /storage/.cache/services/sshd.conf");
	CHECK_STR(fake_calls[1], "/usr/bin/systemctl start sshd");

	fake_reset();
	net_ssh_set(0);
	CHECK_INT(fake_n_calls, 2);
	CHECK_STR(fake_calls[0], "/usr/bin/systemctl stop sshd");
	CHECK_STR(fake_calls[1], "/usr/bin/rm -f /storage/.cache/services/sshd.conf");

	fake_reset();
	fake_reply("/usr/bin/systemctl is-active sshd", "active\n", 0);
	CHECK(net_ssh_enabled());
	fake_reset();
	fake_reply("/usr/bin/systemctl is-active sshd", "inactive\n", 3);
	CHECK(!net_ssh_enabled());
	fake_reset();                  /* systemctl missing: off, not a crash */
	CHECK(!net_ssh_enabled());
}

int main(void)
{
	test_scan();
	test_saved_out_of_range();
	test_min_password();
	test_join();
	test_address();
	test_usb();
	test_wifi_switch();
	test_ssh_switch();
	return check_report("net");
}
