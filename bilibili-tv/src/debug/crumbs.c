/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * For a freeze that prints nothing. The last context switches and interrupt entries are kept in
 * a ring in memory that a reset does not clear (every record is written through the data cache),
 * and a watchdog that a high priority thread feeds once a second resets the SoC when the whole
 * system stops. After the reset the board waits in FEL with the memory intact: read the ring
 * with `xfel read <address of bili_crumbs> 4112 file`, or start the loader again WITHOUT a power cycle
 * (`xfel write 0x20000 loader; xfel exec 0x20000`) and the console prints the end of the ring first
 * (tag: S = switched in, I = interrupt entered, X = interrupt left, then the thread name).
 */

#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
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
static volatile bool armed;

static void put(char mark)
{
	if (!armed) {
		return;
	}
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

/*
 * After a reset without a power cycle the ring of the run before is still in memory: print its end
 * before anything overwrites it (the board is started again from FEL with the loader only).
 */
static int dump_previous(void)
{
	if (bili_crumbs.magic == 0x43525542) {
		uint32_t last = 0;
		int newest = 0;

		for (int i = 0; i < RING; i++) {
			if (bili_crumbs.ring[i].seq > last) {
				last = bili_crumbs.ring[i].seq;
				newest = i;
			}
		}
		printk("crumbs: the run before stopped; its last events, oldest first (seq, cycle counter, tag):\n");
		for (int n = 63; n >= 0; n--) {
			const struct crumb *c = &bili_crumbs.ring[(newest + RING - n) % RING];

			printk("crumbs: %8u %10u %.7s\n", c->seq, c->cycles, c->tag);
		}
	} else {
		printk("crumbs: no ring from an earlier run\n");
	}
	memset(&bili_crumbs, 0, sizeof(bili_crumbs));
	sys_cache_data_flush_range(&bili_crumbs, sizeof(bili_crumbs));
	armed = true;

	return 0;
}
SYS_INIT(dump_previous, PRE_KERNEL_2, 0);

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
	/*
	 * The watchdog is set to reset the CPU core only: the memory controller and the memory stay as
	 * they are, the boot ROM starts again (no boot code in the flash: FEL) and the ring can be read.
	 * Setup selects the reset of the whole system; the core reset is the other value of the config
	 * register, and the mode register is written again after it (arm and restart).
	 */
	sys_write32(0x16aa0000U, 0x06011014U);
	sys_write32(0x16aa0000U | (sys_read32(0x06011018U) & 0xffffU), 0x06011018U);
	sys_write32(0x0a57U << 1 | 1U, 0x06011010U);
	printk("crumbs: core start address 0x%08x, watchdog config 0x%08x\n", sys_read32(0x06010100U),
	       sys_read32(0x06011014U));
	bili_crumbs.magic = 0x43525542;
	for (;;) {
		wdt_feed(wdt, ch);
		k_sleep(K_SECONDS(1));
	}
}

K_THREAD_DEFINE(crumbs_feeder, 1024, feeder, NULL, NULL, NULL, 0, 0, 0);

#if CONFIG_BILI_CRUMBS_TEST_S > 0
/* Checks the chain: the system stops by itself, the watchdog resets it, the ring is read back */
static void stopper(void *a, void *b, void *c)
{
	k_sleep(K_SECONDS(CONFIG_BILI_CRUMBS_TEST_S));
	printk("crumbs: stopping the system for the test\n");
	(void)irq_lock();
	for (;;) {
	}
}
K_THREAD_DEFINE(crumbs_stopper, 1024, stopper, NULL, NULL, NULL, 5, 0, 0);
#endif
