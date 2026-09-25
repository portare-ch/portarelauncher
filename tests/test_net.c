#include "check.h"
#include "fake_proc.h"
#include "net.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#define SAVED "/usr/bin/nmcli -t -f NAME,TYPE connection show"
#define VISIBLE "/usr/bin/nmcli -t -f IN-USE,SSID,SIGNAL,SECURITY device wifi list"

/* The shape of the device's own output, names replaced. Terse mode escapes
 * ':' in values, an SSID can be hidden, and an open network's security is
 * empty. */
static void test_scan(void)
{
	fake_reset();
	fake_reply(SAVED,
		"Home:802-11-wireless\n"
		"lo:loopback\n"
		"Cafe\\:Guest:802-11-wireless\n", 0);
	fake_reply(VISIBLE,
		"*:Home:59:WPA2\n"
		" :Neighbour:45:WPA2\n"
		" :Cafe\\:Guest:40:\n"
		" :Open Net:37:\n"
		" ::30:WPA2\n"
		" :Far Away:12:WPA1 WPA2\n", 0);

	struct net_list l;
	net_scan(&l, 0);
	CHECK(!fake_called("/usr/bin/nmcli device wifi rescan"));

	/* Connected, then saved, then the rest by signal. */
	CHECK_INT(l.n, 5);
	if (l.n == 5) {
		CHECK_STR(l.e[0].name, "Home");
		CHECK(l.e[0].active && l.e[0].saved);
		CHECK_INT(l.e[0].signal, 59);
		CHECK_STR(l.e[0].security, "WPA2");

		CHECK_STR(l.e[1].name, "Cafe:Guest");
		CHECK(l.e[1].saved && !l.e[1].active);
		CHECK_INT(l.e[1].signal, 40);
		CHECK_STR(l.e[1].security, "");

		CHECK_STR(l.e[2].name, "Neighbour");
		CHECK(!l.e[2].saved);
		CHECK_STR(l.e[3].name, "Open Net");
		CHECK_STR(l.e[4].name, "Far Away");
		CHECK_STR(l.e[4].security, "WPA1 WPA2");
	}

	fake_reset();
	fake_reply(SAVED, "", 0);
	fake_reply(VISIBLE, "", 0);
	net_scan(&l, 1);
	CHECK(fake_called("/usr/bin/nmcli device wifi rescan"));
	CHECK_INT(l.n, 0);
}

static void test_saved_out_of_range(void)
{
	fake_reset();
	fake_reply(SAVED, "Holiday Flat:802-11-wireless\n", 0);
	fake_reply(VISIBLE, "", 0);

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
	CHECK_INT(net_min_password("WPA2"), 8);
	CHECK_INT(net_min_password("WPA1 WPA2"), 8);
	CHECK_INT(net_min_password("WPA3"), 1);
	CHECK_INT(net_min_password("WPA2 802.1X"), -1);
}

static void test_join(void)
{
	/* Wrong password: nmcli fails with 4 and the profile it saved on the
	 * way is deleted, so it does not sit in the list looking joinable. */
	fake_reset();
	fake_reply("/usr/bin/nmcli --wait 30 device wifi connect Neighbour password hunter22",
	           "", 4);
	fake_reply("/usr/bin/nmcli connection delete id Neighbour", "", 0);
	CHECK_INT(net_join("Neighbour", "hunter22"), 4);
	CHECK(fake_called("/usr/bin/nmcli connection delete id Neighbour"));

	/* Success keeps it. */
	fake_reset();
	fake_reply("/usr/bin/nmcli --wait 30 device wifi connect Neighbour password hunter22",
	           "", 0);
	CHECK_INT(net_join("Neighbour", "hunter22"), 0);
	CHECK(!fake_called("/usr/bin/nmcli connection delete id Neighbour"));

	/* An open network gets no password argument at all. */
	fake_reset();
	fake_reply("/usr/bin/nmcli --wait 30 device wifi connect Open Net", "", 0);
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
	 * what an official image starts with, and nmcli alone does not lift it. */
	fake_reset();
	fake_reply("/usr/bin/wifictl enable", "", 0);
	fake_reply("/usr/bin/nmcli radio wifi on", "", 0);
	net_wifi_set(1);
	CHECK_INT(fake_n_calls, 2);
	CHECK_STR(fake_calls[0], "/usr/bin/wifictl enable");
	CHECK_STR(fake_calls[1], "/usr/bin/nmcli radio wifi on");

	fake_reset();
	net_wifi_set(0);
	CHECK_STR(fake_calls[0], "/usr/bin/wifictl disable");
	CHECK_STR(fake_calls[1], "/usr/bin/nmcli radio wifi off");

	fake_reset();
	fake_reply("/usr/bin/nmcli -t radio wifi", "enabled\n", 0);
	CHECK(net_wifi_enabled());
	fake_reset();
	fake_reply("/usr/bin/nmcli -t radio wifi", "disabled\n", 0);
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
