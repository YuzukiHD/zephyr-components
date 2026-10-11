/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The Bluetooth link of the AIC8800 chip: the player advertises as a peripheral and only the
 * state of the connection is shown.
 */

#include <stdio.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "features/net/bt.h"

static K_MUTEX_DEFINE(bt_lock);

static struct bt_conn *bt_peer;
static char bt_peer_text[BT_ADDR_LE_STR_LEN];
static enum { BT_OFF, BT_STARTING, BT_READY, BT_LINKED, BT_FAILED } bt_state = BT_OFF;

static const struct bt_data adv_data[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

static void adv_start(void)
{
	int err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, adv_data, ARRAY_SIZE(adv_data), NULL, 0);

	if (err != 0 && err != -EALREADY) {
		printk("bt: advertising failed: %d\n", err);
	}
}

static void bt_connected_cb(struct bt_conn *conn, uint8_t err)
{
	if (err != 0) {
		return;
	}
	if (bt_peer == NULL) {
		bt_peer = bt_conn_ref(conn);
	}
	k_mutex_lock(&bt_lock, K_FOREVER);
	bt_addr_le_to_str(bt_conn_get_dst(conn), bt_peer_text, sizeof(bt_peer_text));
	k_mutex_unlock(&bt_lock);
	bt_state = BT_LINKED;
	printk("bt: connected %s\n", bt_peer_text);
}

static void bt_disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	if (bt_peer != NULL) {
		bt_conn_unref(bt_peer);
		bt_peer = NULL;
	}
	bt_state = BT_READY;
	printk("bt: disconnected (%u)\n", reason);
	adv_start();
}

BT_CONN_CB_DEFINE(bt_conn_cbs) = {
	.connected = bt_connected_cb,
	.disconnected = bt_disconnected_cb,
};

static void bt_main(void *a, void *b, void *c)
{
	int err;

	bt_state = BT_STARTING;
	err = bt_enable(NULL);
	if (err != 0) {
		printk("bt: enable failed: %d\n", err);
		bt_state = BT_FAILED;
		return;
	}
	bt_state = BT_READY;
	adv_start();
}

K_THREAD_STACK_DEFINE(bt_stack, 4096);
static struct k_thread bt_thread;

static int bt_step(void)
{
	k_thread_create(&bt_thread, bt_stack, K_THREAD_STACK_SIZEOF(bt_stack), bt_main, NULL, NULL,
			NULL, 12, 0, K_MSEC(3500));
	k_thread_name_set(&bt_thread, "bt");

	return 0;
}
MP5_INIT(MP5_STAGE_SERVICES, 20, "bluetooth", bt_step);

void bt_status(char *buf, size_t size)
{
	k_mutex_lock(&bt_lock, K_FOREVER);
	switch (bt_state) {
	case BT_OFF:
	case BT_STARTING:
		snprintf(buf, size, "Starting");
		break;
	case BT_READY:
		snprintf(buf, size, "Ready as " CONFIG_BT_DEVICE_NAME);
		break;
	case BT_LINKED:
		snprintf(buf, size, "Connected  %s", bt_peer_text);
		break;
	case BT_FAILED:
		snprintf(buf, size, "Not available");
		break;
	}
	k_mutex_unlock(&bt_lock);
}

bool bt_connected(void)
{
	return bt_state == BT_LINKED;
}
