/* Networking via portnet (iwd) and USB modes via usbgadget.
 * Commands use proc_run without a shell. */
#ifndef PL_NET_H
#define PL_NET_H

#include <stddef.h>

#define NET_MAX 32

struct net_entry {
	char name[80];    /* SSID, or the connection profile's name */
	int  saved;       /* iwd has a passphrase for it */
	int  active;      /* currently connected */
	int  signal;      /* 0-100, or -1 when not seen in a scan */
	char security[32];/* iwd's key type: "psk", "8021x", "wep",
	                     "" when open or not seen in a scan */
};

struct net_list {
	struct net_entry e[NET_MAX];
	int n;
};

/* Known networks first, then anything else in range, strongest first. A
 * network that is both known and visible appears once, marked saved. */
void net_scan(struct net_list *l, int rescan);

int  net_wifi_enabled(void);
/* Sets rfkill and iwd adapter power. Caller persists wifi.enabled. */
void net_wifi_set(int on);

/* sshd.service requires a marker file (ConditionPathExists); toggling SSH
 * updates that marker too. Caller persists ssh.enabled. */
int  net_ssh_enabled(void);
void net_ssh_set(int on);

/* Brings up a saved profile. Returns 0 on success. */
int  net_connect(const char *name);

/* Joins and saves a network; password may be empty for an open network.
 * Returns 0, portnet status 4 (rejected passphrase) or 10 (network gone),
 * or PROC_TIMEOUT. iwd saves credentials only after a successful association. */
int  net_join(const char *ssid, const char *password);

/* Minimum password length; 0 for open, -1 for unsupported 802.1X
 * authentication requiring a username. */
int  net_min_password(const char *security);
int  net_disconnect(void);

/* "192.168.1.42", or empty when not connected. */
void net_address(char *out, size_t osz);

/* Selects an IPv4 address, preferring Wi-Fi. Excludes loopback, link-local
 * and the USB gadget. Returns 1 when found. */
struct ifaddrs;
int  net_address_pick(const struct ifaddrs *list, char *out, size_t osz);

/* USB gadget: disabled, network, file_transfer. */
void usb_modes(char out[][24], int *n, int max);
void usb_mode(char *out, size_t osz);
int  usb_set_mode(const char *mode);
void usb_address(char *out, size_t osz);

#endif
