/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Starts the Wi-Fi chip, scans, and stops it, again and again. Every round prints how many
 * networks the scan found and how much of the malloc arena is free after the stop: a driver that is
 * stopped completely gives all of it back, and finds the chip as new at the next start.
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/printk.h>

#include <aic8800/aic8800.h>

#include "core/app.h"
#include "debug/memcheck.h"

#define ROUNDS		5
#define TOLERANCE	(16U * 1024)

static K_SEM_DEFINE(scan_done, 0, 1);
static int found;
static struct net_mgmt_event_callback cb;

static void scan_event(struct net_mgmt_event_callback *c, uint64_t ev, struct net_if *iface)
{
	if (ev == NET_EVENT_WIFI_SCAN_RESULT) {
		found++;
	} else if (ev == NET_EVENT_WIFI_SCAN_DONE) {
		k_sem_give(&scan_done);
	}
}

static void radio_main(void *a, void *b, void *c)
{
	struct net_if *iface = NULL;
	size_t largest, total, base_total = 0;
	int failures = 0;

	k_sleep(K_SECONDS(15));
	for (int i = 0; i < 100 && iface == NULL; i++) {
		iface = net_if_get_first_wifi();
		k_msleep(100);
	}
	net_mgmt_init_event_callback(&cb, scan_event,
				     NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
	net_mgmt_add_event_callback(&cb);
	/* the boot may have started the driver already */
	aic8800_stop();
	k_sleep(K_SECONDS(1));
	nekobox_mem_probe(&largest, &base_total);
	printk("radio: before the first start: largest block %u KiB, free in total %u KiB\n",
	       (unsigned)(largest / 1024), (unsigned)(base_total / 1024));

	for (int round = 1; round <= ROUNDS; round++) {
		size_t used, peak;
		int64_t t0 = k_uptime_get();
		int ret = aic8800_start();

		printk("radio: round %d: start %d in %d ms\n", round, ret, (int)(k_uptime_get() - t0));
		if (ret == 0) {
			found = 0;
			k_sem_reset(&scan_done);
			ret = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0);
			if (ret == 0) {
				k_sem_take(&scan_done, K_SECONDS(20));
			} else {
				printk("radio: round %d: scan request failed: %d\n", round, ret);
			}
			aic8800_memory(&used, &peak);
			printk("radio: round %d: %d networks, driver has %u KiB (at most %u KiB)\n", round,
			       found, (unsigned)(used / 1024), (unsigned)(peak / 1024));
			failures += found > 0 ? 0 : 1;
		} else {
			failures++;
		}
		t0 = k_uptime_get();
		ret = aic8800_stop();
		k_sleep(K_SECONDS(1));
		nekobox_mem_probe(&largest, &total);
		printk("radio: round %d: stop %d in %d ms, largest block %u KiB, free in total %u KiB\n",
		       round, ret, (int)(k_uptime_get() - t0), (unsigned)(largest / 1024),
		       (unsigned)(total / 1024));
		failures += (total + TOLERANCE >= base_total && largest + TOLERANCE >= base_total) ? 0 : 1;
	}
	printk("radio: %s\n", failures == 0 ? "PASS (0 failures)" : "FAIL");
}

K_THREAD_DEFINE(radio_test, 4096, radio_main, NULL, NULL, NULL, 10, 0, 0);
