/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ff.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>

#include "core/app.h"
#include "core/cfg.h"
#include "core/storage.h"

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = NEKOBOX_MOUNT_POINT,
};
static bool mounted;
static unsigned int generation;
static K_MUTEX_DEFINE(mount_lock);

#define SCAN_MS		3000	/* between two tries to mount a missing card */
#define HEALTH_SCANS	2	/* a mounted card is read every 2nd scan */

bool storage_ready(void)
{
	return mounted;
}

unsigned int storage_generation(void)
{
	return generation;
}

bool storage_space(uint64_t *total, uint64_t *free_bytes)
{
	struct fs_statvfs sv;

	if (!mounted || fs_statvfs(NEKOBOX_MOUNT_POINT, &sv) != 0) {
		return false;
	}
	*total = (uint64_t)sv.f_frsize * sv.f_blocks;
	*free_bytes = (uint64_t)sv.f_frsize * sv.f_bfree;

	return true;
}

/* what the card says, read only: it is never written when the mount fails */
static void report_unreadable(void)
{
	uint32_t sectors = 0;
	static uint8_t sector0[512];

	if (disk_access_init("SD") == 0 &&
	    disk_access_ioctl("SD", DISK_IOCTL_GET_SECTOR_COUNT, &sectors) == 0 &&
	    disk_access_read("SD", sector0, 0, 1) == 0) {
		printk("nekobox: SD sectors %u, sector 0 starts %02x %02x %02x %02x, boot signature %02x%02x\n",
		       sectors, sector0[0], sector0[1], sector0[2], sector0[3], sector0[510], sector0[511]);
	} else {
		printk("nekobox: the card does not answer to reads\n");
	}
}

/* not mounting is no error of the start: the player runs without a card */
static int try_mount(bool first)
{
	int ret;

	k_mutex_lock(&mount_lock, K_FOREVER);
	ret = mounted ? 0 : fs_mount(&mp);
	if (ret == 0 && !mounted) {
		mounted = true;
		generation++;
		printk("nekobox: SD card mounted\n");
		if (!first) {
			cfg_load();
		}
	} else if (ret != 0 && first) {
		printk("nekobox: SD card not available (%d), trying again every %d s\n", ret,
		       SCAN_MS / 1000);
		report_unreadable();
	}
	k_mutex_unlock(&mount_lock);

	return ret;
}

/* a card that is pulled out is not announced: a read of the first sector tells */
static void check_card(void)
{
	static uint8_t sector0[512] __aligned(64);
	static int failed;

	if (disk_access_read("SD", sector0, 0, 1) == 0) {
		failed = 0;
		return;
	}
	if (++failed < 2) {
		return;
	}
	failed = 0;
	k_mutex_lock(&mount_lock, K_FOREVER);
	mounted = false;
	generation++;
	(void)fs_unmount(&mp);
	k_mutex_unlock(&mount_lock);
	printk("nekobox: SD card removed\n");
}

static void storage_watch(void *a, void *b, void *c)
{
	unsigned int scan = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);
	for (;;) {
		k_msleep(SCAN_MS);
		if (nekobox_background_held()) {
			continue;
		}
		if (!mounted) {
			(void)try_mount(false);
		} else if (++scan % HEALTH_SCANS == 0) {
			check_card();
		}
	}
}
K_THREAD_DEFINE(storage_watch_thread, 4096, storage_watch, NULL, NULL, NULL, 14, 0, 0);

static int storage_mount(void)
{
	(void)try_mount(true);

	return 0;
}
NEKOBOX_INIT(NEKOBOX_STAGE_STORAGE, 0, "card", storage_mount);
