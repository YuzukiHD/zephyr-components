/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Raises the CPU clock above what the boot stub sets up (600 MHz) */

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/sun252i-f101-ccu.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"

/* A busy loop over a checksummed block of memory: a CPU that is too fast corrupts it */
static bool cpu_selftest(void)
{
	/* from the malloc arena, so that no RAM stays reserved after the start */
	const unsigned int words = 16 * 1024;
	uint32_t *blk = malloc(words * sizeof(*blk));
	uint32_t sum = 0, ref = 0;
	bool ok = true;

	if (blk == NULL) {
		return true;
	}
	for (unsigned int i = 0; i < words; i++) {
		blk[i] = i * 2654435761U;
	}
	for (int round = 0; ok && round < 50; round++) {
		sum = 0;
		for (unsigned int i = 0; i < words; i++) {
			sum = (sum << 1 | sum >> 31) ^ blk[i] ^ (sum * 3U);
		}
		if (round == 0) {
			ref = sum;
		} else if (sum != ref) {
			ok = false;
		}
	}
	free(blk);

	return ok;
}

void cpu_clock_setup(void)
{
	const struct device *cctl = DEVICE_DT_GET(DT_NODELABEL(cctl));
	uint32_t rate, target = CONFIG_NEKOBOX_CPU_MHZ * 1000000U;

	clock_control_get_rate(cctl, (clock_control_subsys_t)CLK_CPU, &rate);
	/* in steps, each one checked: the voltage is not changed */
	while (target != 0U && rate < target) {
		uint32_t next = MIN(rate + 72000000U, target);

		if (clock_control_set_rate(cctl, (clock_control_subsys_t)CLK_CPU,
					   (clock_control_subsys_rate_t)next) != 0) {
			printk("cpu: %u MHz refused\n", next / 1000000U);
			break;
		}
		if (!cpu_selftest()) {
			printk("cpu: %u MHz BAD\n", next / 1000000U);
			clock_control_set_rate(cctl, (clock_control_subsys_t)CLK_CPU,
					       (clock_control_subsys_rate_t)rate);
			printk("cpu: back to %u MHz\n", rate / 1000000U);
			return;
		}
		rate = next;
	}
	printk("cpu: %u MHz\n", rate / 1000000U);
}

static int cpu_step(void)
{
	cpu_clock_setup();

	return 0;
}
NEKOBOX_INIT(NEKOBOX_STAGE_EARLY, 10, "cpu clock", cpu_step);
