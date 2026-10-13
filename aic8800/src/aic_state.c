/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The static data of the driver is in two sections of its own (aic_state.ld). The kernel does not
 * set them up at boot, this file does, and puts them back to the start values when the driver is
 * shut down, so that the next start finds the driver as it was after the boot.
 */

#include <string.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

extern char __aic_bss_start[], __aic_bss_end[];
extern char __aic_data_start[], __aic_data_end[], __aic_data_load_start[];

void aic_state_reset(void)
{
	memset(__aic_bss_start, 0, __aic_bss_end - __aic_bss_start);
	memcpy(__aic_data_start, __aic_data_load_start, __aic_data_end - __aic_data_start);
}

static int aic_state_init(void)
{
	aic_state_reset();

	return 0;
}
SYS_INIT(aic_state_init, PRE_KERNEL_1, 0);
