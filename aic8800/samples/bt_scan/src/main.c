/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Starts the Bluetooth controller of the chip, lists the advertising devices around (one line per
 * address, strongest first) and, when CONFIG_SAMPLE_BT_CONNECT_ADDR is set, connects to that
 * device and lists its GATT services.
 */

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/printk.h>

#define MAX_DEVICES	64
#define UUID_XIAOMI	0xFE95

struct dev {
	bt_addr_le_t addr;
	int8_t rssi;
	uint16_t reports;
	bool connectable;
	bool xiaomi;
	char name[24];
};

static struct dev devs[MAX_DEVICES];
static int ndev;
static unsigned int total;

static K_SEM_DEFINE(connected_sem, 0, 1);
static K_SEM_DEFINE(disc_sem, 0, 1);
static struct bt_conn *conn;
static int conn_err;

struct ad_info {
	char name[24];
	bool xiaomi;
};

static bool ad_cb(struct bt_data *data, void *user_data)
{
	struct ad_info *info = user_data;

	switch (data->type) {
	case BT_DATA_NAME_COMPLETE:
	case BT_DATA_NAME_SHORTENED: {
		size_t n = MIN(data->data_len, sizeof(info->name) - 1U);

		memcpy(info->name, data->data, n);
		info->name[n] = '\0';
		break;
	}
	case BT_DATA_SVC_DATA16:
		if (data->data_len >= 2 && sys_get_le16(data->data) == UUID_XIAOMI) {
			info->xiaomi = true;
		}
		break;
	case BT_DATA_UUID16_SOME:
	case BT_DATA_UUID16_ALL:
		for (int i = 0; i + 2 <= data->data_len; i += 2) {
			if (sys_get_le16(&data->data[i]) == UUID_XIAOMI) {
				info->xiaomi = true;
			}
		}
		break;
	default:
		break;
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	struct ad_info info = { 0 };
	struct dev *d = NULL;

	total++;
	for (int i = 0; i < ndev; i++) {
		if (bt_addr_le_cmp(&devs[i].addr, addr) == 0) {
			d = &devs[i];
			break;
		}
	}
	if (d == NULL) {
		if (ndev == MAX_DEVICES) {
			return;
		}
		d = &devs[ndev++];
		d->addr = *addr;
		d->rssi = rssi;
	}

	bt_data_parse(ad, ad_cb, &info);
	d->reports++;
	if (rssi > d->rssi) {
		d->rssi = rssi;
	}
	if (type == BT_GAP_ADV_TYPE_ADV_IND || type == BT_GAP_ADV_TYPE_ADV_DIRECT_IND) {
		d->connectable = true;
	}
	if (info.name[0] != '\0') {
		strcpy(d->name, info.name);
	}
	d->xiaomi |= info.xiaomi;
}

static bool is_wanted(const char *name)
{
	return CONFIG_SAMPLE_BT_FIND_NAME[0] != '\0' && strstr(name, CONFIG_SAMPLE_BT_FIND_NAME) != NULL;
}

static void print_table(void)
{
	bool used[MAX_DEVICES] = { false };

	printk("\n%d reports, %d devices\n", total, ndev);
	for (int n = 0; n < ndev; n++) {
		int best = -1;
		char str[BT_ADDR_LE_STR_LEN];

		for (int i = 0; i < ndev; i++) {
			if (!used[i] && (best < 0 || devs[i].rssi > devs[best].rssi)) {
				best = i;
			}
		}
		used[best] = true;
		bt_addr_le_to_str(&devs[best].addr, str, sizeof(str));
		printk("%2d  %s  rssi %4d  reports %3u  %s%s  %s%s\n", n + 1, str, devs[best].rssi,
		       devs[best].reports, devs[best].connectable ? "connectable" : "-",
		       devs[best].xiaomi ? " XIAOMI" : "", devs[best].name,
		       is_wanted(devs[best].name) ? "   <== " CONFIG_SAMPLE_BT_FIND_NAME : "");
	}
}

static void connected_cb(struct bt_conn *c, uint8_t err)
{
	conn_err = err;
	k_sem_give(&connected_sem);
}

static void disconnected_cb(struct bt_conn *c, uint8_t reason)
{
	printk("disconnected, reason 0x%02x\n", reason);
}

BT_CONN_CB_DEFINE(conn_cbs) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
};

static uint8_t discover_cb(struct bt_conn *c, const struct bt_gatt_attr *attr,
			   struct bt_gatt_discover_params *params)
{
	char uuid[BT_UUID_STR_LEN];
	const struct bt_gatt_service_val *svc;

	if (attr == NULL) {
		k_sem_give(&disc_sem);
		return BT_GATT_ITER_STOP;
	}
	svc = attr->user_data;
	bt_uuid_to_str(svc->uuid, uuid, sizeof(uuid));
	printk("  service %s  handles 0x%04x..0x%04x\n", uuid, attr->handle, svc->end_handle);

	return BT_GATT_ITER_CONTINUE;
}

static void connect_to(const char *want)
{
	struct bt_gatt_discover_params params = {
		.uuid = NULL,
		.func = discover_cb,
		.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE,
		.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE,
		.type = BT_GATT_DISCOVER_PRIMARY,
	};
	int err;

	for (int i = 0; i < ndev; i++) {
		char str[BT_ADDR_LE_STR_LEN];

		bt_addr_le_to_str(&devs[i].addr, str, sizeof(str));
		if (strncmp(str, want, 17) != 0) {
			continue;
		}
		printk("connecting to %s\n", str);
		err = bt_conn_le_create(&devs[i].addr, BT_CONN_LE_CREATE_CONN,
					BT_LE_CONN_PARAM_DEFAULT, &conn);
		if (err != 0) {
			printk("connect request failed: %d\n", err);
			return;
		}
		if (k_sem_take(&connected_sem, K_SECONDS(15)) != 0) {
			printk("no connection after 15 s\n");
			bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
			bt_conn_unref(conn);
			return;
		}
		if (conn_err != 0) {
			printk("connection failed, error 0x%02x\n", conn_err);
			bt_conn_unref(conn);
			return;
		}
		printk("connected, discovering services\n");
		err = bt_gatt_discover(conn, &params);
		if (err == 0) {
			k_sem_take(&disc_sem, K_SECONDS(15));
		} else {
			printk("discover failed: %d\n", err);
		}
		bt_conn_disconnect(conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
		bt_conn_unref(conn);
		k_sleep(K_SECONDS(1));
		return;
	}
	printk("%s was not seen in the scan\n", want);
}

int main(void)
{
	struct bt_le_scan_param param = {
		.type = BT_LE_SCAN_TYPE_ACTIVE,
		.options = BT_LE_SCAN_OPT_NONE,
		.interval = BT_GAP_SCAN_FAST_INTERVAL,
		.window = BT_GAP_SCAN_FAST_WINDOW,
	};
	int err;

	printk("starting Bluetooth\n");
	err = bt_enable(NULL);
	if (err != 0) {
		printk("bt_enable failed: %d\n", err);
		return 0;
	}
	printk("Bluetooth ready, scanning\n");

	err = bt_le_scan_start(&param, scan_cb);
	if (err != 0) {
		printk("scan failed: %d\n", err);
		return 0;
	}
	k_sleep(K_SECONDS(CONFIG_SAMPLE_BT_SCAN_SECONDS));
	bt_le_scan_stop();
	print_table();

	if (CONFIG_SAMPLE_BT_CONNECT_ADDR[0] != '\0') {
		connect_to(CONFIG_SAMPLE_BT_CONNECT_ADDR);
	}
	printk("done\n");

	return 0;
}
