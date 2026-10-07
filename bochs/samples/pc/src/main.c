/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>
#include <ff.h>
#include <zephyr/fs/fs.h>
#include <stdlib.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/kernel.h>

int bochs_run(int argc, char *argv[]);
void bx_mem_file(const char *name, const char *data, size_t len);

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

static char rc[1024];

#ifdef CONFIG_SAMPLE_PC_TEST_PATTERN
#define TW 720
#define TH 400

/* eight colour bars, a white frame, a diagonal and a marker in the top left corner */
static void test_pattern(void)
{
	static const uint16_t bars[8] = {0xffff, 0xffe0, 0x07ff, 0x07e0, 0xf81f, 0xf800, 0x001f, 0x0000};
	const struct device *disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	uint16_t *pic = aligned_alloc(64, TW * TH * 2);
	struct display_sunxi_rgb img = {
		.data = pic, .width = TW, .height = TH, .stride = TW * 2,
	};

	for (int y = 0; y < TH; y++) {
		for (int x = 0; x < TW; x++) {
			uint16_t c = bars[x * 8 / TW];

			if (x < 2 || y < 2 || x >= TW - 2 || y >= TH - 2 || x == y) {
				c = 0xffff;
			}
			if (x < 40 && y < 40) {
				c = 0xf800;
			}
			pic[y * TW + x] = c;
		}
	}
	sys_cache_data_flush_range(pic, TW * TH * 2);
	printk("test pattern: %d\n", display_sunxi_show_rgb(disp, &img));
	while (1) {
		k_sleep(K_SECONDS(10));
	}
}
#endif

int main(void)
{
#ifdef CONFIG_SAMPLE_PC_TEST_PATTERN
	test_pattern();
#endif
	static char *argv[] = {"bochs", "-q", "-f", "mem:bochsrc", NULL};
	int n;
	int ret;

	ret = fs_mount(&mp);

	printk("sd mount: %d\n", ret);
	if (ret == 0) {
		struct fs_dir_t dir;
		struct fs_dirent ent;

		fs_dir_t_init(&dir);
		if (fs_opendir(&dir, "/SD:") == 0) {
			while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
				printk("sd: %s %s %u\n", ent.type == FS_DIR_ENTRY_DIR ? "dir " : "file",
				       ent.name, (unsigned int)ent.size);
			}
			fs_closedir(&dir);
		}
	}

	if (ret == 0 && CONFIG_SAMPLE_PC_DISK[0] != '\0') {
		/* Bochs leaves "<image>.lock" behind when it does not exit cleanly */
		char lock[96];

		snprintf(lock, sizeof(lock), "%s.lock", CONFIG_SAMPLE_PC_DISK);
		printk("lock file removed: %d\n", fs_unlink(lock));
	}
#ifdef CONFIG_SAMPLE_PC_VERIFY
	if (ret == 0 && CONFIG_SAMPLE_PC_DISK[0] != '\0') {
		static uint32_t table[256];
		static uint8_t buf[32768];
		struct fs_file_t f;
		uint32_t crc = 0xffffffffU;
		ssize_t got;

		for (uint32_t i = 0; i < 256; i++) {
			uint32_t c = i;

			for (int k = 0; k < 8; k++) {
				c = (c >> 1) ^ (0xedb88320U & -(c & 1));
			}
			table[i] = c;
		}
		fs_file_t_init(&f);
		if (fs_open(&f, CONFIG_SAMPLE_PC_DISK, FS_O_READ) == 0) {
			while ((got = fs_read(&f, buf, sizeof(buf))) > 0) {
				for (ssize_t i = 0; i < got; i++) {
					crc = table[(crc ^ buf[i]) & 0xff] ^ (crc >> 8);
				}
			}
			fs_close(&f);
			printk("crc32 %s %08x\n", CONFIG_SAMPLE_PC_DISK, (unsigned int)~crc);
		}
	}
#endif
#ifdef CONFIG_SAMPLE_PC_PATCH_AUTOEXEC
	if (ret == 0 && CONFIG_SAMPLE_PC_DISK[0] != '\0') {
		static const char autoexec[] = "rem     \r\nREM VBMOUSE.EXE      ";
		struct fs_file_t f;

		fs_file_t_init(&f);
		if (fs_open(&f, CONFIG_SAMPLE_PC_DISK, FS_O_RDWR) == 0) {
			ret = fs_seek(&f, 63808512, FS_SEEK_SET);
			if (ret == 0) {
				ret = fs_write(&f, autoexec, sizeof(autoexec) - 1);
			}
			fs_close(&f);
			printk("autoexec patched: %d\n", ret);
		}
	}
#endif

	n = snprintf(rc, sizeof(rc),
		     "romimage: file=mem:BIOS-bochs-legacy\n"
		     "vgaromimage: file=mem:VGABIOS-lgpl-latest\n"
		     "megs: %d\n"
		     "cpu: count=1, ips=%d\n"
		     "boot: disk\n"
		     "display_library: nogui\n"
		     "vga: extension=none\n"
		     "mouse: enabled=0\n"
		     "clock: sync=none, time0=1000000000\n"
		     "port_e9_hack: enabled=1\n"
		     "log: -\n"
		     "panic: action=fatal\n"
		     "error: action=ignore\n"
		     "info: action=ignore\n"
		     "debug: action=ignore\n",
		     CONFIG_SAMPLE_PC_MEGS, CONFIG_SAMPLE_PC_IPS);
	if (CONFIG_SAMPLE_PC_DISK[0] != '\0') {
		n += snprintf(rc + n, sizeof(rc) - n,
			      "ata0-master: type=disk, path=\"%s\", mode=flat, cylinders=%d, heads=%d, spt=%d\n",
			      CONFIG_SAMPLE_PC_DISK, CONFIG_SAMPLE_PC_DISK_CYLINDERS,
			      CONFIG_SAMPLE_PC_DISK_HEADS, CONFIG_SAMPLE_PC_DISK_SPT);
	}
	bx_mem_file("bochsrc", rc, n);

	return bochs_run(4, argv);
}
