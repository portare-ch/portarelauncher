/* Bluetooth, through bluetoothctl and the enable script the OS already has.
 *
 * The headline use is a pair of headphones at night, which is why connect
 * does the whole job - pair, trust, connect - rather than making the user
 * find the difference between them on a games console.
 *
 * On "auto-connect to known devices": the mechanism is BlueZ's Trusted flag,
 * not a loop of our own. Headphones reconnect by calling the host when they
 * are switched on, and BlueZ accepts that without asking only for a trusted
 * device. So the toggle sets Trusted on every paired device and records the
 * choice, so anything paired later gets the same treatment. Turning it on
 * also makes one attempt from this side, for headphones that were already
 * on before the launcher started.
 *
 * The limit worth stating: that attempt is one shot at the moment you ask
 * for it. Nothing here polls, because a connect to a device that is not
 * there does not fail - measured on the device, it never returns at all -
 * and this program has one thread and owns the panel. Every call goes
 * through proc_run_for's ceiling for the same reason.
 */
#ifndef PL_BT_H
#define PL_BT_H

#include <stddef.h>

#define BT_MAX 32

struct bt_device {
	char addr[20];    /* AA:BB:CC:DD:EE:FF                       */
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

/* Is the adapter up? This asks the adapter rather than reading the setting,
 * because the two disagree whenever the service failed to start. */
int  bt_powered(void);

/* Goes through portareos-bluetooth, which also starts and stops the pairing
 * agent and records controllers.bluetooth.enabled. Slow - seconds. */
void bt_power(int on);

/* Pairs and trusts if it has to, then connects. Returns 0 on success.
 * Blocks; a device that is not listening takes the full page timeout. */
int  bt_connect(const char *addr);
int  bt_disconnect(const char *addr);

int  bt_autoconnect(void);
void bt_set_autoconnect(int on);

#endif
