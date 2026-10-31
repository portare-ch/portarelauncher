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

/* NULL uses synchronous deadlines. The UI supplies an input-aware runner;
 * a nonzero command result stops the operation, including cancellation. */
struct bt_client {
	int (*run)(void *ctx, char *const argv[], void (*cb)(char *, void *),
	           void *cb_ctx, int ceiling_ms);
	void *ctx;
};

/* Failed/cancelled queries retain the previous list. */
int bt_list(const struct bt_client *c, struct bt_list *l);
int bt_scan(const struct bt_client *c, struct bt_list *l, int seconds);

/* 0/1 on success, negative on query failure. */
int bt_powered(const struct bt_client *c);
int bt_power(const struct bt_client *c, int on);

/* Reuses existing pairing/trust; stops at the first failed step. */
int bt_connect(const struct bt_client *c, const char *addr);
int bt_disconnect(const struct bt_client *c, const char *addr);

int bt_autoconnect(void);
int bt_set_autoconnect(const struct bt_client *c, int on);

#endif
