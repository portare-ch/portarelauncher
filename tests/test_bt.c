#include "check.h"
#include "fake_proc.h"
#include "bt.h"
#include "proc.h"

#define BTCTL "/usr/bin/bluetoothctl"

static const struct bt_device *find(const struct bt_list *l, const char *addr)
{
	for (int i = 0; i < l->n; i++)
		if (strcmp(l->d[i].addr, addr) == 0)
			return &l->d[i];
	return NULL;
}

static void test_list(void)
{
	fake_reset();
	fake_reply(BTCTL " devices Connected",
		"Device 11:22:33:44:55:66 Headphones\n", 0);
	/* Paired but nameless: kept, because the user paired it. */
	fake_reply(BTCTL " devices Paired",
		"Device 11:22:33:44:55:66 Headphones\n"
		"Device AA:BB:CC:DD:EE:01 AA-BB-CC-DD-EE-01\n", 0);
	fake_reply(BTCTL " devices Trusted", "", 0);
	fake_reply(BTCTL " devices",
		"Device 11:22:33:44:55:66 Headphones\n"
		/* A beacon with no name: noise, dropped. */
		"Device C0:28:8D:87:51:82 C0-28-8D-87-51-82\n"
		/* bluetoothctl colours names even into a pipe. */
		"Device 22:33:44:55:66:77 \033[1;39mPad\033[0m\n"
		/* Running commentary, not the listing. */
		"[\033[0;92mNEW\033[0m] Device 33:44:55:66:77:88 Ghost\n"
		"[DEL] Device 44:55:66:77:88:99 Gone\n"
		"Device 55:66:77:88:99:AA Keyboard\n", 0);

	struct bt_list l;
	bt_list(NULL, &l);

	CHECK_INT(l.n, 4);
	CHECK(find(&l, "C0:28:8D:87:51:82") == NULL);
	CHECK(find(&l, "33:44:55:66:77:88") == NULL);
	CHECK(find(&l, "44:55:66:77:88:99") == NULL);

	/* Connected first, then paired, then by name. */
	if (l.n == 4) {
		CHECK_STR(l.d[0].name, "Headphones");
		CHECK(l.d[0].connected && l.d[0].paired);
		CHECK_STR(l.d[1].addr, "AA:BB:CC:DD:EE:01");
		CHECK(l.d[1].paired && !l.d[1].connected);
		CHECK_STR(l.d[2].name, "Keyboard");
		CHECK_STR(l.d[3].name, "Pad");
	}
}

static void test_name_arrives_later(void)
{
	/* Paired under its address, named in a later listing: the name wins. */
	fake_reset();
	fake_reply(BTCTL " devices Connected", "", 0);
	fake_reply(BTCTL " devices Paired",
		"Device AA:BB:CC:DD:EE:02 AA-BB-CC-DD-EE-02\n", 0);
	fake_reply(BTCTL " devices Trusted", "", 0);
	fake_reply(BTCTL " devices",
		"Device AA:BB:CC:DD:EE:02 Speaker\n", 0);

	struct bt_list l;
	bt_list(NULL, &l);
	CHECK_INT(l.n, 1);
	CHECK_STR(l.d[0].name, "Speaker");
	CHECK(l.d[0].paired);
}

static void test_bluetoothctl_missing(void)
{
	/* Nothing answers: an empty list, not a crash. */
	fake_reset();
	struct bt_list l = {0};
	CHECK_INT(bt_list(NULL, &l), -1);
	CHECK_INT(l.n, 0);
}

static void test_many(void)
{
	/* More devices than the list holds: capped, not overrun. */
	static char out[8192];
	out[0] = '\0';
	for (int i = 0; i < BT_MAX + 10; i++) {
		char line[64];
		snprintf(line, sizeof(line), "Device 00:00:00:00:00:%02X Device %d\n", i, i);
		strcat(out, line);
	}
	fake_reset();
	fake_reply(BTCTL " devices Connected", "", 0);
	fake_reply(BTCTL " devices Paired", "", 0);
	fake_reply(BTCTL " devices Trusted", "", 0);
	fake_reply(BTCTL " devices", out, 0);

	struct bt_list l;
	bt_list(NULL, &l);
	CHECK_INT(l.n, BT_MAX);
}

static void test_pair_failure(void)
{
	fake_reset();
	fake_reply(BTCTL " info 11:22:33:44:55:66", "Paired: no\nTrusted: no\nConnected: no\n", 0);
	fake_reply(BTCTL " pair 11:22:33:44:55:66", "", 1);
	fake_reply(BTCTL " trust 11:22:33:44:55:66", "", 0);
	fake_reply(BTCTL " connect 11:22:33:44:55:66", "", 0);
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), 1);
	CHECK(!fake_called(BTCTL " trust 11:22:33:44:55:66"));
	CHECK(!fake_called(BTCTL " connect 11:22:33:44:55:66"));
}

static void connection_reply(const char *info)
{
	fake_reset();
	fake_reply(BTCTL " info 11:22:33:44:55:66", info, 0);
	fake_reply(BTCTL " pair 11:22:33:44:55:66", "", 0);
	fake_reply(BTCTL " trust 11:22:33:44:55:66", "", 0);
	fake_reply(BTCTL " connect 11:22:33:44:55:66", "", 0);
}

static void test_reconnect(void)
{
	connection_reply("Device 11:22:33:44:55:66 Headset\n"
	                 "\tPaired: \033[1;39myes\033[0m\n"
	                 "\tTrusted: yes\n\tConnected: no\n");
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), 0);
	CHECK_INT(fake_n_calls, 2);
	CHECK_STR(fake_calls[1], BTCTL " connect 11:22:33:44:55:66");

	connection_reply("Paired: yes\nTrusted: no\nConnected: no\n");
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), 0);
	CHECK_INT(fake_n_calls, 3);
	CHECK_STR(fake_calls[1], BTCTL " trust 11:22:33:44:55:66");
	CHECK_STR(fake_calls[2], BTCTL " connect 11:22:33:44:55:66");

	connection_reply("Paired: no\nTrusted: no\nConnected: no\n");
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), 0);
	CHECK_INT(fake_n_calls, 4);
	CHECK_STR(fake_calls[1], BTCTL " pair 11:22:33:44:55:66");
	CHECK_STR(fake_calls[2], BTCTL " trust 11:22:33:44:55:66");
	CHECK_STR(fake_calls[3], BTCTL " connect 11:22:33:44:55:66");

	/* A headset may connect itself while this menu's cached list is open. */
	connection_reply("Paired: yes\nTrusted: yes\nConnected: yes\n");
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), 0);
	CHECK_INT(fake_n_calls, 1);
}

static void test_connection_failures(void)
{
	connection_reply("Paired: yes\nConnected: no\n");
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), PROC_FAILED);
	CHECK_INT(fake_n_calls, 1);

	/* A remote name containing a property must not be treated as that property. */
	connection_reply("Device 11:22:33:44:55:66 Paired: yes\n"
	                 "Trusted: yes\nConnected: no\n");
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), PROC_FAILED);
	CHECK_INT(fake_n_calls, 1);

	fake_reset();
	fake_reply(BTCTL " info 11:22:33:44:55:66",
	           "Paired: yes\nTrusted: no\nConnected: no\n", 0);
	fake_reply(BTCTL " trust 11:22:33:44:55:66", "", 1);
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), 1);
	CHECK_INT(fake_n_calls, 2);
	CHECK(!fake_called(BTCTL " connect 11:22:33:44:55:66"));

	fake_reset();
	fake_reply(BTCTL " info 11:22:33:44:55:66", "", PROC_TIMEOUT);
	CHECK_INT(bt_connect(NULL, "11:22:33:44:55:66"), PROC_TIMEOUT);
	CHECK_INT(fake_n_calls, 1);
}

static int cancelled(void *ctx, char *const argv[], void (*cb)(char *, void *),
                     void *cb_ctx, int ceiling_ms)
{
	(*(int *)ctx)++;
	return PROC_CANCELLED;
}

static void test_failed_refresh(void)
{
	struct bt_list l = { .n = 1, .d = {{ .name = "Still connected", .connected = 1 }} };
	fake_reset();
	fake_reply(BTCTL " devices Connected", "Device AA:BB:CC:DD:EE:FF Other\n", 0);
	fake_reply(BTCTL " devices Paired", "", PROC_TIMEOUT);
	CHECK_INT(bt_list(NULL, &l), PROC_TIMEOUT);
	CHECK_INT(fake_n_calls, 2);
	CHECK_INT(l.n, 1);
	CHECK_STR(l.d[0].name, "Still connected");
	CHECK(l.d[0].connected);

	int calls = 0;
	struct bt_client client = { .run = cancelled, .ctx = &calls };
	CHECK_INT(bt_list(&client, &l), PROC_CANCELLED);
	CHECK_INT(calls, 1);
	CHECK_STR(l.d[0].name, "Still connected");
	CHECK_INT(bt_connect(&client, "11:22:33:44:55:66"), PROC_CANCELLED);
	CHECK_INT(calls, 2);
	CHECK_INT(bt_scan(&client, &l, 8), PROC_CANCELLED);
	CHECK_INT(calls, 3);
	CHECK_STR(l.d[0].name, "Still connected");
}

int main(void)
{
	test_reconnect();
	test_connection_failures();
	test_failed_refresh();
	test_pair_failure();
	test_list();
	test_name_arrives_later();
	test_bluetoothctl_missing();
	test_many();
	return check_report("bt");
}
