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
#include "core/storage.h"

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MP5_MOUNT_POINT,
};
static bool mounted;

bool storage_ready(void)
{
	return mounted;
}

bool storage_space(uint64_t *total, uint64_t *free_bytes)
{
	struct fs_statvfs sv;

	if (!mounted || fs_statvfs(MP5_MOUNT_POINT, &sv) != 0) {
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
		printk("mp5: SD sectors %u, sector 0 starts %02x %02x %02x %02x, boot signature %02x%02x\n",
		       sectors, sector0[0], sector0[1], sector0[2], sector0[3], sector0[510], sector0[511]);
	} else {
		printk("mp5: the card does not answer to reads\n");
	}
}

/* not mounting is no error of the start: the player runs without a card */
static int storage_mount(void)
{
	int ret = fs_mount(&mp);

	mounted = ret == 0;
	printk("mp5: SD card %s (%d)\n", mounted ? "mounted" : "not available", ret);
	if (!mounted) {
		report_unreadable();
	}

	return 0;
}
MP5_INIT(MP5_STAGE_STORAGE, 0, "card", storage_mount);
