/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* Starts the Bluetooth controller of the chip and lists the advertising devices around. */

#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static int found;

static bool name_cb(struct bt_data *data, void *user_data)
{
	char *name = user_data;

	if (data->type == BT_DATA_NAME_COMPLETE || data->type == BT_DATA_NAME_SHORTENED) {
		size_t n = MIN(data->data_len, 31U);

		memcpy(name, data->data, n);
		name[n] = '\0';
		return false;
	}
	return true;
}

static void scan_cb(const bt_addr_le_t *addr, int8_t rssi, uint8_t type, struct net_buf_simple *ad)
{
	char str[BT_ADDR_LE_STR_LEN];
	char name[32] = "";

	bt_data_parse(ad, name_cb, name);
	bt_addr_le_to_str(addr, str, sizeof(str));
	printk("%2d  %s  rssi %4d  type %u  %s\n", ++found, str, rssi, type, name);
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
	printk("Bluetooth ready\n");

	err = bt_le_scan_start(&param, scan_cb);
	if (err != 0) {
		printk("scan failed: %d\n", err);
		return 0;
	}
	k_sleep(K_SECONDS(CONFIG_SAMPLE_BT_SCAN_SECONDS));
	bt_le_scan_stop();
	printk("%d devices\n", found);

	return 0;
}
