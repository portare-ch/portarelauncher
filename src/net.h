/* Networking, through the tools the system already has.
 *
 * Wi-Fi is NetworkManager with an iwd backend, and NetworkManager already
 * keeps a profile per network with its credentials. Multiple saved SSIDs are
 * not a feature to add - they exist, there has simply been nothing able to
 * pick between them. wifictl is built around a single "wifi.ssid" setting,
 * which is the limitation rather than the interface, so this talks to nmcli.
 *
 * USB gadget goes through /usr/bin/usbgadget, which already has the interface
 * a menu wants: --options lists the modes, no argument reports the current
 * one, and a mode name sets it.
 *
 * Both are run through proc_run, which never involves a shell. See proc.h.
 */
#ifndef PL_NET_H
#define PL_NET_H

#include <stddef.h>

#define NET_MAX 32

struct net_entry {
	char name[80];    /* SSID, or the connection profile's name */
	int  saved;       /* NetworkManager has credentials for it  */
	int  active;      /* currently connected                    */
	int  signal;      /* 0-100, or -1 when not seen in a scan   */
	char security[32];/* "WPA2", "WPA2 WPA3", "" when open or
	                     not seen in a scan                     */
};

struct net_list {
	struct net_entry e[NET_MAX];
	int n;
};

/* Saved profiles first, then anything else in range, strongest first. A
 * network that is both saved and visible appears once, marked saved. */
void net_scan(struct net_list *l, int rescan);

int  net_wifi_enabled(void);
void net_wifi_set(int on);

/* Brings up a saved profile. Returns 0 on success. */
int  net_connect(const char *name);

/* Joins a network that has no saved profile, saving one if it works.
 * password may be empty for an open network. Returns 0 on success, or
 * nmcli's exit status: 4 when activation failed (almost always the
 * password), 10 when the network is no longer there, or PROC_TIMEOUT.
 *
 * A failed join leaves nothing behind. NetworkManager saves the profile
 * before it knows whether the password was right, so a wrong one would
 * otherwise sit in the saved list looking like a network you can join. */
int  net_join(const char *ssid, const char *password);

/* The shortest password the network's security allows, or -1 when it needs
 * more than a password - a username too, for 802.1X - which this cannot
 * ask for. 0 means open: no password at all. */
int  net_min_password(const char *security);
int  net_disconnect(void);

/* "192.168.1.42", or empty when not connected. */
void net_address(char *out, size_t osz);

/* USB gadget: disabled, network, file_transfer. */
void usb_modes(char out[][24], int *n, int max);
void usb_mode(char *out, size_t osz);
int  usb_set_mode(const char *mode);
void usb_address(char *out, size_t osz);

#endif
