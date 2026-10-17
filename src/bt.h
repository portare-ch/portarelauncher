/* Bluetooth via bluetoothctl and portareos-bluetooth.
 *
 * Auto-connect uses BlueZ's Trusted flag for incoming connections. Enabling
 * it trusts paired devices and makes one detached outgoing attempt per device.
 * Synchronous calls use proc_run_for deadlines because bluetoothctl can hang. */
#ifndef PL_BT_H
#define PL_BT_H

#include <stddef.h>

#define BT_MAX 32

struct bt_device {
	char addr[20];    /* AA:BB:CC:DD:EE:FF */
	char name[64];    /* the device's name, or its address again */
	int  paired;
	int  trusted;
	int  connected;
};

struct bt_list {
	struct bt_device d[BT_MAX];
	int n;
};

/* Connected first, then paired, then whatever else a scan turned up. */
void bt_list(struct bt_list *l);

/* Discovers for `seconds`, then lists. Blocks for that long. */
void bt_scan(struct bt_list *l, int seconds);

/* Queries the adapter; the saved setting may differ if service startup failed. */
int  bt_powered(void);

/* Runs portareos-bluetooth to manage power, the pairing agent and the saved
 * setting. Blocks for seconds. */
void bt_power(int on);

/* Pairs, trusts and connects. Returns 0 on success. An unavailable device
 * may consume the full connection timeout. */
int  bt_connect(const char *addr);
int  bt_disconnect(const char *addr);

int  bt_autoconnect(void);
void bt_set_autoconnect(int on);

#endif
