/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * For a freeze that prints nothing. The last context switches and interrupt entries are kept in
 * a ring in memory that a reset does not clear (every record is written through the data cache),
 * and a watchdog that a high priority thread feeds once a second resets the SoC when the whole
 * system stops. After the reset the board waits in FEL; the ring is read from there with
 * `xfel hexdump <address of bili_crumbs> 4096` (entries of 16 bytes: sequence, cycle counter, tag;
 * the tag is a mark and the name of the thread that was running: S = switched in, I = interrupt
 * entered, X = interrupt left). The newest entry has the highest sequence number.
 */

#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define RING 256

struct crumb {
	uint32_t seq;
	uint32_t cycles;
	char tag[8];
};

struct crumbs {
	uint32_t magic;
	uint32_t pad[3];
	struct crumb ring[RING];
};

struct crumbs bili_crumbs __noinit __aligned(64);
static uint32_t seq;

static void put(char mark)
{
	struct crumb *c = &bili_crumbs.ring[seq % RING];
	const char *name = k_thread_name_get(k_current_get());

	c->seq = ++seq;
	c->cycles = k_cycle_get_32();
	c->tag[0] = mark;
	strncpy(&c->tag[1], name != NULL ? name : "?", sizeof(c->tag) - 2);
	c->tag[7] = '\0';
	sys_cache_data_flush_range(c, sizeof(*c));
}

void sys_trace_thread_switched_in_user(void)
{
	put('S');
}

void sys_trace_isr_enter_user(void)
{
	put('I');
}

void sys_trace_isr_exit_user(void)
{
	put('X');
}

static void feeder(void *a, void *b, void *c)
{
	const struct device *wdt = DEVICE_DT_GET(DT_NODELABEL(wdt));
	struct wdt_timeout_cfg cfg = {
		.window.max = CONFIG_BILI_CRUMBS_TIMEOUT_MS,
		.flags = WDT_FLAG_RESET_SOC,
	};
	int ch = wdt_install_timeout(wdt, &cfg);

	if (ch < 0 || wdt_setup(wdt, 0) != 0) {
		printk("crumbs: the watchdog does not start (%d)\n", ch);
		return;
	}
	bili_crumbs.magic = 0x43525542;
	for (;;) {
		wdt_feed(wdt, ch);
		k_sleep(K_SECONDS(1));
	}
}

K_THREAD_DEFINE(crumbs_feeder, 1024, feeder, NULL, NULL, NULL, 0, 0, 0);
