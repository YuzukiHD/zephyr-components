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
 */

#include <errno.h>
#include <string.h>
#include <ff.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/fs/fs.h>
#include <stdlib.h>
#include <zephyr/cache.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "lv_rvv.h"
#ifdef CONFIG_SAMPLE_MP5_G2D
#include "lv_g2d.h"
#endif
#include "mp5.h"
#include "player.h"

#define MOUNT_PT	"/SD:"

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MOUNT_PT,
};
static bool mounted;

bool storage_ready(void)
{
	return mounted;
}

bool storage_space(uint64_t *total, uint64_t *free_bytes)
{
	struct fs_statvfs sv;

	if (!mounted || fs_statvfs(MOUNT_PT, &sv) != 0) {
		return false;
	}
	*total = (uint64_t)sv.f_frsize * sv.f_blocks;
	*free_bytes = (uint64_t)sv.f_frsize * sv.f_bfree;

	return true;
}

#ifdef CONFIG_SAMPLE_MP5_SELFTEST
/* The files in the root of the card, to see what there is to play */
static void list_root(void)
{
	struct fs_dir_t dir;
	struct fs_dirent ent;

	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, MOUNT_PT) != 0) {
		return;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		printk("sd: %s %s %u\n", ent.type == FS_DIR_ENTRY_DIR ? "dir " : "file", ent.name,
		       (unsigned int)ent.size);
	}
	fs_closedir(&dir);
}

/* Drives the player the way the controls do and prints where it is after each step */
static void selftest_main(void *a, void *b, void *c)
{
	static const char *const names[] = {"idle", "playing", "paused", "ended", "error"};
	static const struct {
		const char *what;
		int wait_ms;
	} steps[] = {
		{"play", 2500}, {"seek 6000", 2500}, {"pause", 1500}, {"seek 1000 while paused", 1500},
		{"resume", 2500}, {"volume 30", 1500}, {"volume 0", 1000}, {"volume 70", 500},
		{"seek 9500", 4000}, {"replay", 2000},
	};

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	k_msleep(1500);
	for (int i = 0; i < ARRAY_SIZE(steps); i++) {
		const char *w = steps[i].what;

		if (strcmp(w, "seek 6000") == 0) {
			player_seek(6000);
		} else if (strcmp(w, "pause") == 0) {
			player_pause();
		} else if (strcmp(w, "seek 1000 while paused") == 0) {
			player_seek(1000);
		} else if (strcmp(w, "resume") == 0) {
			player_play();
		} else if (strcmp(w, "volume 30") == 0) {
			player_set_volume(30);
		} else if (strcmp(w, "volume 0") == 0) {
			player_set_volume(0);
		} else if (strcmp(w, "volume 70") == 0) {
			player_set_volume(70);
		} else if (strcmp(w, "seek 9500") == 0) {
			player_seek(9500);
		} else if (strcmp(w, "replay") == 0) {
			player_play();
		}
		printk("selftest: %-24s -> ", w);
		k_msleep(steps[i].wait_ms);
		printk("%s, position %u ms, volume %d\n", names[player_state()], player_position_ms(),
		       player_volume());
	}
	printk("selftest: done\n");
}

K_THREAD_DEFINE(selftest, 4096, selftest_main, NULL, NULL, NULL, 9, 0, -1);
#endif

int main(void)
{
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int ret;

	cpu_clock_setup();
	if (!lv_rvv_selftest()) {
		printk("mp5: the vector blends differ from LVGL, switched off\n");
		lv_rvv_enable(false);
	}
	if (!device_is_ready(disp)) {
		printk("mp5: the display is not ready\n");
		return 0;
	}
	ret = fs_mount(&mp);
	mounted = ret == 0;
	printk("mp5: SD card %s (%d)\n", mounted ? "mounted" : "not available", ret);

#ifdef CONFIG_SAMPLE_MP5_G2D
	lv_g2d_init();
#endif
	ui_init();
#ifdef CONFIG_SAMPLE_MP5_SELFTEST
	list_root();
	ui_goto(SCR_PLAYER);
	k_thread_start(selftest);
#endif
	lv_timer_handler();
	display_blanking_off(disp);
#ifdef CONFIG_SAMPLE_MP5_UI_BENCH
	/* a full redraw of the home screen, with and without the vector blends */
	for (int pass = 0; pass < 2; pass++) {
		int64_t t0;

		lv_rvv_enable(pass == 0);
		lv_obj_invalidate(lv_screen_active());
		lv_refr_now(NULL);
		t0 = k_uptime_get();
		for (int i = 0; i < 10; i++) {
			lv_obj_invalidate(lv_screen_active());
			lv_refr_now(NULL);
		}
		printk("mp5: 10 full redraws of the home screen, vector blends %s: %d ms\n",
		       pass == 0 ? "on" : "off", (int)(k_uptime_get() - t0));
#ifdef CONFIG_SAMPLE_MP5_G2D
		lv_g2d_report();
#endif
	}
	lv_rvv_enable(true);
#endif
#ifdef CONFIG_SAMPLE_MP5_UI_PROFILE
	{
		extern void ui_profile_start(struct k_thread *target);
		int64_t t0 = k_uptime_get();

		ui_profile_start(k_current_get());
		while (k_uptime_get() - t0 < 9000) {
			lv_obj_invalidate(lv_screen_active());
			lv_refr_now(NULL);
		}
	}
#endif
#ifdef CONFIG_SAMPLE_MP5_UI_DEMO
	ui_stats_demo();
#endif
	printk("mp5: %d x %d\n", (int)lv_display_get_horizontal_resolution(NULL),
	       (int)lv_display_get_vertical_resolution(NULL));

	/* how long the user interface takes to draw, so that a slow redraw shows in the log */
	for (;;) {
		static uint32_t slow, worst_us, report_ms;
		uint32_t t0 = k_cycle_get_32(), next, us;

		next = lv_timer_handler();
		us = k_cyc_to_us_floor32(k_cycle_get_32() - t0);
		if (us > 30000U) {
			slow++;
		}
		worst_us = MAX(worst_us, us);
		if (k_uptime_get_32() - report_ms >= 5000U) {
			if (worst_us > 8000U) {
				printk("ui: worst redraw %u ms, %u slower than 30 ms\n", worst_us / 1000,
				       slow);
			}
			slow = worst_us = 0;
			report_ms = k_uptime_get_32();
		}
		k_msleep(MIN(next, 20U));
	}

	return 0;
}
