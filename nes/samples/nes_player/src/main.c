/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Runs an NES ROM from the SD card. Real time comes from the sound: the
 * emulation stops while the audio ring holds enough.
 */

#include <errno.h>
#include <ff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/printk.h>
#include <nes_zephyr/nesemu.h>

#include "app.h"

#define RATE		CONFIG_SAMPLE_NES_RATE
/* the emulation waits while this many frames of sound are queued */
#define HIGH_WATER	(RATE / 15)
#define REPORT_MS	5000
#define SAVE_MS		3000

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

static char rom_path[128], save_path[128];

/* the configured ROM, or else the first .nes file in the root of the card */
static int pick_rom(void)
{
	struct fs_dirent ent;
	struct fs_dir_t dir;
	char *dot;

	strncpy(rom_path, CONFIG_SAMPLE_NES_ROM, sizeof(rom_path) - 1);
	if (fs_stat(rom_path, &ent) != 0) {
		fs_dir_t_init(&dir);
		if (fs_opendir(&dir, "/SD:") != 0) {
			return -ENOENT;
		}
		rom_path[0] = '\0';
		while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
			size_t n = strlen(ent.name);

			if (ent.type == FS_DIR_ENTRY_FILE && n > 4 &&
			    (strcmp(ent.name + n - 4, ".nes") == 0 ||
			     strcmp(ent.name + n - 4, ".NES") == 0)) {
				snprintf(rom_path, sizeof(rom_path), "/SD:/%s", ent.name);
				break;
			}
		}
		fs_closedir(&dir);
		if (rom_path[0] == '\0') {
			return -ENOENT;
		}
	}
	strncpy(save_path, rom_path, sizeof(save_path) - 5);
	dot = strrchr(save_path, '.');
	strcpy(dot != NULL && dot > strrchr(save_path, '/') ? dot : save_path + strlen(save_path),
	       ".sav");

	return 0;
}

int main(void)
{
	static int16_t pcm[2 * 2048];
	int64_t t_report, t_save;
	uint32_t frames = 0, emu_us = 0, pushed = 0;
	int ret;

	ret = fs_mount(&mp);
	if (ret != 0) {
		printk("cannot mount the SD card: %d\n", ret);
		return 0;
	}
	if (video_init() != 0 || audio_init(RATE) != 0) {
		printk("video or audio not available\n");
		return 0;
	}
	if (pick_rom() != 0) {
		printk("no .nes file on the card\n");
		return 0;
	}
	printk("ROM %s, save %s\n", rom_path, save_path);
	ret = nesemu_open(rom_path, save_path, RATE);
	if (ret != 0) {
		printk("cannot start %s: %d\n", rom_path, ret);
		return 0;
	}
	pad_start();

#ifdef CONFIG_SAMPLE_NES_BENCH
	{
		/* no sound pacing, no display: the cost of the emulation alone */
		uint32_t t0 = k_cycle_get_32(), worst = 0;
		const int n_frames = CONFIG_SAMPLE_NES_BENCH_FRAMES;

		for (int i = 0; i < n_frames; i++) {
			uint32_t c0 = k_cycle_get_32();

			nesemu_set_frame_buffer(video_acquire());
			nesemu_set_frameskip(0);
			nesemu_run_frame();
			worst = MAX(worst, k_cycle_get_32() - c0);
			if ((i + 1) % 200 == 0) {
				printk("bench: frame %d crc %08x\n", i + 1,
				       (uint32_t)crc32_ieee((const uint8_t *)video_acquire(),
							    NESEMU_WIDTH * 2 * nesemu_height()));
			}
		}
		printk("bench: %d frames, %u us/frame, worst %u us\n", n_frames,
		       (uint32_t)(k_cyc_to_us_floor64(k_cycle_get_32() - t0) / n_frames),
		       (uint32_t)k_cyc_to_us_floor64(worst));
		return 0;
	}
#endif
	t_report = t_save = k_uptime_get();
	while (true) {
		uint32_t c0 = k_cycle_get_32();
		size_t n;

		nesemu_set_keys(pad_keys());
		nesemu_set_frame_buffer(video_acquire());
		/* a frame that runs late is not drawn, the sound has priority */
		nesemu_set_frameskip(audio_queued() < RATE / 30 ? 1 : 0);
		if (nesemu_run_frame()) {
			video_present(NESEMU_WIDTH, nesemu_height());
		}
		emu_us += k_cyc_to_us_floor32(k_cycle_get_32() - c0);
		while ((n = nesemu_audio_read(pcm, ARRAY_SIZE(pcm) / 2)) > 0) {
			size_t done = 0;

			pushed += n;
			while (done < n) {
				done += audio_push(pcm + done * 2, n - done);
				if (done < n) {
					k_msleep(1);
				}
			}
		}
		frames++;

		while (audio_queued() > HIGH_WATER) {
			k_msleep(1);
		}
		if (k_uptime_get() - t_save >= SAVE_MS) {
			nesemu_save_flush();
			t_save = k_uptime_get();
		}
		if (k_uptime_get() - t_report >= REPORT_MS) {
			printk("nes: %u frames, emulation %u us/frame, sound %u frames pushed, "
			       "queued %u, underruns %u, ring empty %u, slab full %u\n", frames,
			       emu_us / frames, pushed, (unsigned int)audio_queued(),
			       audio_underruns(), audio_starved(), audio_slab_waits());
			frames = emu_us = pushed = 0;
			t_report = k_uptime_get();
		}
	}

	return 0;
}
