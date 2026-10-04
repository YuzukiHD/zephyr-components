/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/* Runs Doom from a WAD on the SD card. */

#include <errno.h>
#include <ff.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <doom_zephyr/doom.h>

#include "app.h"

#define RATE		CONFIG_SAMPLE_DOOM_RATE
#define REPORT_MS	5000

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

static char wad_path[128];

/* the configured WAD, or else the first .wad file in the root of the card */
static int pick_wad(void)
{
	struct fs_dirent ent;
	struct fs_dir_t dir;

	strncpy(wad_path, CONFIG_SAMPLE_DOOM_WAD, sizeof(wad_path) - 1);
	if (fs_stat(wad_path, &ent) == 0) {
		return 0;
	}
	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, "/SD:") != 0) {
		return -ENOENT;
	}
	wad_path[0] = '\0';
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		size_t n = strlen(ent.name);

		if (ent.type == FS_DIR_ENTRY_FILE && n > 4 &&
		    (strcmp(ent.name + n - 4, ".wad") == 0 || strcmp(ent.name + n - 4, ".WAD") == 0)) {
			snprintf(wad_path, sizeof(wad_path), "/SD:/%s", ent.name);
			break;
		}
	}
	fs_closedir(&dir);

	return wad_path[0] == '\0' ? -ENOENT : 0;
}

int main(void)
{
	int64_t t_report;
	uint32_t frames = 0, drawn = 0;
	int ret;

	ret = fs_mount(&mp);
	if (ret != 0) {
		printk("cannot mount the SD card: %d\n", ret);
		return 0;
	}
	if (video_init() != 0) {
		printk("video not available\n");
		return 0;
	}
	doom_audio_init(RATE);
	if (audio_init(RATE) != 0) {
		printk("audio not available\n");
	}
	if (pick_wad() != 0) {
		printk("no .wad file on the card\n");
		return 0;
	}
	printk("WAD %s\n", wad_path);
	doom_set_frame_buffer(video_acquire());
	pad_start();
	ret = doom_open(wad_path, "/SD:");
	if (ret != 0) {
		printk("cannot start Doom: %d\n", ret);
		return 0;
	}

	t_report = k_uptime_get();
	while (true) {
		doom_set_frame_buffer(video_acquire());
		if (doom_run_frame()) {
			video_present(DOOM_WIDTH, DOOM_HEIGHT);
			drawn++;
		}
		frames++;
		if (k_uptime_get() - t_report >= REPORT_MS) {
			printk("doom: %u passes, %u pictures, sound underruns %u\n", frames, drawn,
			       audio_underruns());
			frames = drawn = 0;
			t_report = k_uptime_get();
		}
	}

	return 0;
}
