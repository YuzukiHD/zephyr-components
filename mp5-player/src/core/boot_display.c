/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The panel needs about half a second to come up (a fixed sequence of waits), so the display is
 * initialised in a thread of its own while the CPU clock is raised, the vector code is checked and
 * the card is mounted. The UI stage waits for it and then starts LVGL.
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <lvgl_zephyr.h>

#include "core/app.h"

static K_SEM_DEFINE(display_up, 0, 1);
static int display_ret;
static K_THREAD_STACK_DEFINE(display_init_stack, 8192);
static struct k_thread display_init_thread;

static void display_init_main(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	display_ret = device_init(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)));
	k_sem_give(&display_up);
}

static int display_start(void)
{
	k_thread_create(&display_init_thread, display_init_stack, K_THREAD_STACK_SIZEOF(display_init_stack),
			display_init_main, NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&display_init_thread, "display_init");

	return 0;
}
MP5_INIT(MP5_STAGE_EARLY, 0, "display thread", display_start);

static int display_wait_and_lvgl(void)
{
	k_sem_take(&display_up, K_FOREVER);
	if (display_ret != 0 || !device_is_ready(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)))) {
		printk("mp5: the display is not ready (%d)\n", display_ret);
		return -ENODEV;
	}

	return lvgl_init();
}
MP5_INIT(MP5_STAGE_UI, 0, "display up, LVGL", display_wait_and_lvgl);
