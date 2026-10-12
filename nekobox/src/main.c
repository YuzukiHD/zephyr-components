/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * NEKOBOX: a small media player for the YuzukiNeko. A home screen with a clock leads to the video
 * player, the file browser, the photo viewer and the USB second screen. The user interface is
 * LVGL on the ARGB frame buffer plane; the video, the photos and the second screen go to the
 * video plane underneath. The whole image runs in place from the flash (XIP).
 *
 * main() only runs the start stages; what happens in them is registered by the modules with
 * NEKOBOX_INIT (core/app.h).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "ui/ui.h"

int main(void)
{
	static const enum nekobox_stage stages[] = {
		NEKOBOX_STAGE_EARLY, NEKOBOX_STAGE_STORAGE, NEKOBOX_STAGE_UI, NEKOBOX_STAGE_SERVICES,
	};

	printk("nekobox: boot %u ms: main starts\n", k_uptime_get_32());
	for (int i = 0; i < ARRAY_SIZE(stages); i++) {
		if (nekobox_run_stage(stages[i]) != 0) {
			printk("nekobox: start failed in stage %d\n", stages[i]);
			return 0;
		}
	}
	ui_present();
	nekobox_run_stage(NEKOBOX_STAGE_READY);
	ui_loop();

	return 0;
}
