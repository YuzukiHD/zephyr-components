/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * The frame of the console is RGB565 and goes to the scaling video plane of
 * the display engine as it is. The emulator draws the next frame into its own
 * buffer while the plane still scans out the last one, so each frame is copied
 * into one of three buffers (the one shown last stays valid for one more
 * refresh).
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <doom_zephyr/doom.h>

#include "app.h"

#define STRIDE	(DOOM_WIDTH * 2)
#define BUF_SZ	ROUND_UP(STRIDE * DOOM_HEIGHT, 64)
#define NBUF	4

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static uint8_t *buf[NBUF];
static unsigned int cur;
static atomic_t ready_idx;
static unsigned int ready_w, ready_h;

K_SEM_DEFINE(frame_sem, 0, 1);
K_THREAD_STACK_DEFINE(video_stack, 8192);
static struct k_thread video_thread;

/*
 * Putting a picture up can wait for the next refresh; that must not hold the
 * emulation, which is paced by the sound. Only the newest picture is shown.
 */
static void video_main(void *a, void *b, void *c)
{
	bool warned = false;

	while (true) {
		struct display_sunxi_rgb img = {.width = 0};
		int idx, ret;

		k_sem_take(&frame_sem, K_FOREVER);
		idx = atomic_get(&ready_idx);
		img.data = buf[idx];
		img.width = ready_w;
		img.height = ready_h;
		img.stride = STRIDE;
		img.nonblock = false;
		ret = display_sunxi_show_rgb(disp, &img);
		if (ret != 0 && !warned) {
			printk("video: show_rgb failed: %d\n", ret);
			warned = true;
		}
	}
}

int video_init(void)
{
	if (!device_is_ready(disp)) {
		return -ENODEV;
	}
	for (int i = 0; i < NBUF; i++) {
		buf[i] = aligned_alloc(64, BUF_SZ);
		if (buf[i] == NULL) {
			return -ENOMEM;
		}
	}
	k_thread_create(&video_thread, video_stack, K_THREAD_STACK_SIZEOF(video_stack), video_main,
			NULL, NULL, NULL, 5, 0, K_NO_WAIT);
	k_thread_name_set(&video_thread, "video");

	return 0;
}

uint16_t *video_acquire(void)
{
	return (uint16_t *)buf[(cur + 1) % NBUF];
}

void video_present(unsigned int width, unsigned int height)
{
	cur = (cur + 1) % NBUF;
	sys_cache_data_flush_range(buf[cur], STRIDE * height);
	ready_w = width;
	ready_h = height;
	atomic_set(&ready_idx, cur);
	k_sem_give(&frame_sem);
}
