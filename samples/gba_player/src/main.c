/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Runs a Game Boy Advance ROM from the SD card. Real time comes from the
 * sound: the emulation stops while the audio ring holds enough.
 */

#include <errno.h>
#include <ff.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/dt-bindings/clock/sun252i-f101-ccu.h>
#include <zephyr/sys/printk.h>
#include <mgba_zephyr/gba.h>

#include "app.h"

#define RATE		CONFIG_SAMPLE_GBA_RATE
/* the emulation waits while this many frames of sound are queued */
#define HIGH_WATER	(RATE / 15)
/*
 * Below SKIP_BELOW queued sound frames the emulation catches up by drawing one frame in four,
 * until the queue has grown to SKIP_RESUME again. The gap keeps it from falling back to
 * the normal rate, which a heavy scene cannot keep up with, as soon as the queue recovers.
 */
#define SKIP_BELOW	1500
#define SKIP_RESUME	2800
#define SKIP_COUNT	3
/* the frames skipped between two drawn ones in normal running */
#define SKIP_BASE	(CONFIG_SAMPLE_GBA_VIDEO_DIV - 1)
#define REPORT_MS	5000
#define SAVE_MS		3000

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

/* A busy loop over a checksummed block of memory: a CPU that is too fast corrupts it */
static bool cpu_selftest(void)
{
	static uint32_t blk[16 * 1024];
	uint32_t sum = 0, ref;

	for (unsigned int i = 0; i < ARRAY_SIZE(blk); i++) {
		blk[i] = i * 2654435761U;
	}
	for (int round = 0; round < 50; round++) {
		sum = 0;
		for (unsigned int i = 0; i < ARRAY_SIZE(blk); i++) {
			sum = (sum << 1 | sum >> 31) ^ blk[i] ^ (sum * 3U);
		}
		if (round == 0) {
			ref = sum;
		} else if (sum != ref) {
			return false;
		}
	}

	return true;
}

static void cpu_clock_setup(void)
{
	const struct device *cctl = DEVICE_DT_GET(DT_NODELABEL(cctl));
	uint32_t rate, target = CONFIG_SAMPLE_GBA_CPU_MHZ * 1000000U;

	clock_control_get_rate(cctl, (clock_control_subsys_t)CLK_CPU, &rate);
	/* in steps, each one checked: the voltage is not changed */
	while (target != 0U && rate < target) {
		uint32_t next = MIN(rate + 72000000U, target);
		bool ok;

		if (clock_control_set_rate(cctl, (clock_control_subsys_t)CLK_CPU,
					   (clock_control_subsys_rate_t)next) != 0) {
			printk("cpu: %u MHz refused\n", next / 1000000U);
			break;
		}
		ok = cpu_selftest();
		if (!ok) {
			printk("cpu: %u MHz BAD\n", next / 1000000U);
			clock_control_set_rate(cctl, (clock_control_subsys_t)CLK_CPU,
					       (clock_control_subsys_rate_t)rate);
			printk("cpu: back to %u MHz\n", rate / 1000000U);
			return;
		}
		rate = next;
	}
}

static char rom_path[128], save_path[128];

static bool ext_ok(const char *name, size_t n, bool gba_only)
{
	static const char *const ext[] = {".gba", ".GBA", ".gbc", ".GBC", ".gb", ".GB"};

	for (int i = 0; i < ARRAY_SIZE(ext); i++) {
		size_t e = strlen(ext[i]);

		if (gba_only && i >= 2) {
			break;
		}
		if (n > e && strcmp(name + n - e, ext[i]) == 0) {
			return true;
		}
	}

	return false;
}

/* the configured ROM, or else the first fitting file in the root of the card; GBA ROMs first */
static int pick_rom(void)
{
	struct fs_dirent ent;
	struct fs_dir_t dir;
	char *dot;

	strncpy(rom_path, CONFIG_SAMPLE_GBA_ROM, sizeof(rom_path) - 1);
	if (fs_stat(rom_path, &ent) != 0) {
		fs_dir_t_init(&dir);
		if (fs_opendir(&dir, "/SD:") != 0) {
			return -ENOENT;
		}
		rom_path[0] = '\0';
		for (int pass = 0; pass < 2 && rom_path[0] == '\0'; pass++) {
			fs_closedir(&dir);
			if (fs_opendir(&dir, "/SD:") != 0) {
				return -ENOENT;
			}
			while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
				size_t n = strlen(ent.name);

				if (ent.type == FS_DIR_ENTRY_FILE && n > 4 &&
				    ext_ok(ent.name, n, pass == 0) &&
				    ent.size <= CONFIG_MGBA_MAX_ROM_SIZE_MB * 1024UL * 1024UL) {
					snprintf(rom_path, sizeof(rom_path), "/SD:/%s", ent.name);
					break;
				}
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

#ifdef CONFIG_SAMPLE_GBA_ROM_DIAG
/* how much of each ROM file is real data, and what random reads from the card cost */
static void rom_diag(void)
{
	struct fs_dirent ent;
	struct fs_dir_t dir;
	static const uint32_t chunks[] = {4096, 16384, 65536, 262144};
	uint8_t *buf = aligned_alloc(64, 262144);

	fs_dir_t_init(&dir);
	if (buf == NULL || fs_opendir(&dir, "/SD:") != 0) {
		return;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		char path[160];
		struct fs_file_t f;
		size_t n = strlen(ent.name);
		uint32_t pad = 0, pad_end = 0;
		uint32_t seed = 12345;

		if (ent.type != FS_DIR_ENTRY_FILE || !ext_ok(ent.name, n, false)) {
			continue;
		}
		snprintf(path, sizeof(path), "/SD:/%s", ent.name);
		fs_file_t_init(&f);
		if (fs_open(&f, path, FS_O_READ) != 0) {
			continue;
		}
		/* trailing 0xFF / 0x00 bytes */
		for (uint32_t end = ent.size; end > 0; ) {
			uint32_t len = MIN(end, 65536U);
			bool all = true;

			fs_seek(&f, end - len, FS_SEEK_SET);
			fs_read(&f, buf, len);
			for (uint32_t i = len; i > 0; i--) {
				uint8_t c = buf[i - 1];

				if (c != 0xFF && c != 0x00) {
					all = false;
					pad += len - i;
					break;
				}
			}
			if (!all) {
				break;
			}
			pad += len;
			end -= len;
		}
		pad_end = ent.size - pad;
		printk("rom: %s size %u, data ends at %u (padding %u)\n", ent.name, (unsigned int)ent.size,
		       pad_end, pad);
		for (int c = 0; c < ARRAY_SIZE(chunks); c++) {
			uint32_t t0 = k_cycle_get_32();
			const int reads = 64;

			for (int i = 0; i < reads; i++) {
				seed = seed * 1103515245U + 12345U;
				uint32_t off = ((seed >> 8) % (ent.size / chunks[c])) * chunks[c];

				fs_seek(&f, off, FS_SEEK_SET);
				fs_read(&f, buf, chunks[c]);
			}
			printk("rom: %s random %u KiB reads: %u us each\n", ent.name, chunks[c] / 1024,
			       (uint32_t)(k_cyc_to_us_floor64(k_cycle_get_32() - t0) / reads));
		}
		fs_close(&f);
	}
	fs_closedir(&dir);
	free(buf);
}
#endif

int main(void)
{
	static int16_t pcm[2 * 2048];
	int64_t t_report, t_save;
	uint32_t frames = 0, emu_us = 0, pushed = 0, skipped = 0;
	bool catching_up = false;
	int drawn;
	int ret;

	cpu_clock_setup();
	ret = fs_mount(&mp);
	if (ret != 0) {
		printk("cannot mount the SD card: %d\n", ret);
		return 0;
	}
	if (video_init() != 0 || audio_init(RATE) != 0) {
		printk("video or audio not available\n");
		return 0;
	}
#ifdef CONFIG_SAMPLE_GBA_ROM_DIAG
	rom_diag();
	return 0;
#endif
	if (pick_rom() != 0) {
		printk("no .gba/.gb/.gbc file on the card\n");
		return 0;
	}
	printk("ROM %s, save %s\n", rom_path, save_path);
	ret = gba_open(rom_path, save_path, RATE);
	if (ret != 0) {
		printk("cannot start %s: %d\n", rom_path, ret);
		return 0;
	}
	pad_start();
#ifdef CONFIG_SAMPLE_GBA_PROFILE
	{
		extern void profile_start(struct k_thread *target);

		profile_start(k_current_get());
	}
#endif

#ifdef CONFIG_SAMPLE_GBA_BENCH
	{
		/* no sound pacing, no skipping, no display: the cost of the emulation alone */
		static int16_t sink[2 * 2048];
		uint32_t t0 = k_cycle_get_32(), worst = 0;
		const int n_frames = CONFIG_SAMPLE_GBA_BENCH_FRAMES;

		for (int i = 0; i < n_frames; i++) {
			uint32_t c0 = k_cycle_get_32();

			gba_set_frame_buffer(video_acquire());
			gba_set_frameskip(CONFIG_SAMPLE_GBA_BENCH_FRAMESKIP);
			gba_run_frame();
			while (gba_audio_read(sink, ARRAY_SIZE(sink) / 2) > 0) {
			}
			worst = MAX(worst, k_cycle_get_32() - c0);
			if ((i + 1) % 200 == 0) {
				unsigned int w, h;
				const uint16_t *f = gba_frame(&w, &h);

				printk("bench: frame %d crc %08x\n", i + 1,
				       (uint32_t)crc32_ieee((const uint8_t *)f, w * 2 * h));
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

		gba_set_keys(pad_keys());
		gba_set_frame_buffer(video_acquire());
		/* behind the sound: draw less until it has caught up */
		{
			size_t queued = audio_queued();

			if (queued < SKIP_BELOW) {
				catching_up = true;
			} else if (queued > SKIP_RESUME) {
				catching_up = false;
			}
			gba_set_frameskip(catching_up ? MAX(SKIP_COUNT, SKIP_BASE) : SKIP_BASE);
		}
		drawn = gba_run_frame();
		emu_us += k_cyc_to_us_floor32(k_cycle_get_32() - c0);
		if (drawn) {
			unsigned int w, h;

			gba_frame(&w, &h);
			video_present(w, h);
		} else {
			skipped++;
		}
		while ((n = gba_audio_read(pcm, ARRAY_SIZE(pcm) / 2)) > 0) {
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
			gba_save_flush();
			t_save = k_uptime_get();
		}
		if (k_uptime_get() - t_report >= REPORT_MS) {
			printk("gba: %u frames, emulation %u us/frame, sound %u frames pushed, "
			       "queued %u, underruns %u, ring empty %u, slab full %u, skipped %u\n", frames,
			       emu_us / frames, pushed, (unsigned int)audio_queued(),
			       audio_underruns(), audio_starved(), audio_slab_waits(), skipped);
			frames = emu_us = pushed = skipped = 0;
			t_report = k_uptime_get();
		}
	}

	return 0;
}
