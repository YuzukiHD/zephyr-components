/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Bluetooth part of the AIC8800 chip is only checked: the controller is started, it
 * advertises for a moment, and everything is stopped again, so that Bluetooth costs no memory
 * and no power while nobody uses it. The result is shown in the settings.
 */

#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "features/net/bt.h"

static volatile enum { BT_UNCHECKED, BT_CHECKING, BT_WORKS, BT_FAILED } bt_state;

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

void bt_probe(void)
{
	int err;

	bt_state = BT_CHECKING;
	err = bt_enable(NULL);
	if (err != 0) {
		printk("bt: enable failed: %d\n", err);
		bt_state = BT_FAILED;
		return;
	}
	err = bt_le_adv_start(BT_LE_ADV_NCONN, adv_data, ARRAY_SIZE(adv_data), NULL, 0);
	if (err != 0) {
		printk("bt: advertising failed: %d\n", err);
	} else {
		k_msleep(300);
		bt_le_adv_stop();
	}
	if (bt_disable() != 0) {
		printk("bt: disable failed\n");
	}
	bt_state = err == 0 ? BT_WORKS : BT_FAILED;
	printk("bt: checked, %s\n", err == 0 ? "works" : "does not answer");
}

void bt_status(char *buf, size_t size)
{
	switch (bt_state) {
	case BT_UNCHECKED:
	case BT_CHECKING:
		snprintf(buf, size, "Checking");
		break;
	case BT_WORKS:
		snprintf(buf, size, "Works, off when not used");
		break;
	case BT_FAILED:
		snprintf(buf, size, "Not available");
		break;
	}
}
