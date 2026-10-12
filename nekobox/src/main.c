/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * MP5: a small media player for the YuzukiNeko. A home screen with a clock leads to the video
 * player, the file browser, the photo viewer and the USB second screen. The user interface is
 * LVGL on the ARGB frame buffer plane; the video, the photos and the second screen go to the
 * video plane underneath. The whole image runs in place from the flash (XIP).
 *
 * main() only runs the start stages; what happens in them is registered by the modules with
 * MP5_INIT (core/app.h).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "ui/ui.h"

int main(void)
{
	static const enum mp5_stage stages[] = {
		MP5_STAGE_EARLY, MP5_STAGE_STORAGE, MP5_STAGE_UI, MP5_STAGE_SERVICES,
	};

	printk("mp5: boot %u ms: main starts\n", k_uptime_get_32());
	for (int i = 0; i < ARRAY_SIZE(stages); i++) {
		if (mp5_run_stage(stages[i]) != 0) {
			printk("mp5: start failed in stage %d\n", stages[i]);
			return 0;
		}
	}
	ui_present();
	mp5_run_stage(MP5_STAGE_READY);
	ui_loop();

	return 0;
}
