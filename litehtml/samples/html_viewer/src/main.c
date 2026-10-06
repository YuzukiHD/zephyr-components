/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Shows an HTML page from the SD card (or a built in one) on the LCD, rendered by litehtml.
 * A page that is taller than the screen scrolls by itself.
 */

#include <ff.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/fs/fs.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <litehtml_zephyr/lh.h>

#define W 1024
#define H 600
#define BG 0xffff

extern const uint8_t cfb_font_1016[95][20];

static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static FATFS fat_fs;
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = "/SD:",
};

static const char builtin_page[] =
	"<html><head><title>litehtml</title><style>"
	"body{margin:24px;background:#f4f6fb;color:#222}"
	"h1{color:#1a4fa0}.box{background:#fff;border:2px solid #1a4fa0;padding:12px}"
	"</style></head><body><h1>litehtml on the F101</h1>"
	"<div class=\"box\"><p>No page found on the SD card. Put a <b>page.html</b> in the root "
	"of the card.</p></div></body></html>";

static bool has_suffix_nocase(const char *name, const char *suffix)
{
	size_t n = strlen(name), e = strlen(suffix);

	if (n <= e) {
		return false;
	}
	for (size_t i = 0; i < e; i++) {
		char c = name[n - e + i];

		if (c >= 'A' && c <= 'Z') {
			c += 'a' - 'A';
		}
		if (c != suffix[i]) {
			return false;
		}
	}
	return true;
}

static bool name_is_html(const char *name)
{
	return has_suffix_nocase(name, ".html") || has_suffix_nocase(name, ".htm");
}

/* the configured file, else the first .html/.htm in the root of the card */
static bool pick_file(char *path, size_t size)
{
	struct fs_dirent ent;
	struct fs_dir_t dir;

	strncpy(path, CONFIG_SAMPLE_HTML_FILE, size - 1);
	path[size - 1] = '\0';
	if (fs_stat(path, &ent) == 0) {
		return true;
	}
	fs_dir_t_init(&dir);
	if (fs_opendir(&dir, "/SD:") != 0) {
		return false;
	}
	while (fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		if (ent.type == FS_DIR_ENTRY_FILE && name_is_html(ent.name)) {
			snprintk(path, size, "/SD:/%s", ent.name);
			fs_closedir(&dir);
			return true;
		}
	}
	fs_closedir(&dir);
	return false;
}

static char *read_file(const char *path)
{
	struct fs_dirent ent;
	struct fs_file_t f;
	char *buf;
	ssize_t n;

	if (fs_stat(path, &ent) != 0) {
		return NULL;
	}
	buf = malloc(ent.size + 1);
	if (buf == NULL) {
		return NULL;
	}
	fs_file_t_init(&f);
	if (fs_open(&f, path, FS_O_READ) != 0) {
		free(buf);
		return NULL;
	}
	n = fs_read(&f, buf, ent.size);
	fs_close(&f);
	if (n < 0) {
		free(buf);
		return NULL;
	}
	buf[n] = '\0';
	return buf;
}

/* the largest block malloc can still give, which is about the free heap while it is not fragmented */
static size_t largest_free(void)
{
	size_t lo = 0, hi = 16U << 20;

	while (hi - lo > 4096) {
		size_t mid = (lo + hi) / 2;
		void *p = malloc(mid);

		if (p != NULL) {
			free(p);
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return lo;
}

int main(void)
{
	static const struct lh_font font = {
		.glyphs = cfb_font_1016, .first = 32, .last = 126, .cell_w = 10, .cell_h = 16,
	};
	char path[128];
	char *html = NULL;
	const char *source = "built in";
	struct lh_page *page;
	/* two frames: the one on the screen is never drawn into, or the scan out sees it half done */
	uint16_t *fbs[2];
	int cur = 0;
	int64_t t0;
	int height, scroll = 0, dir = 1;

	if (!device_is_ready(disp)) {
		printk("display not ready\n");
		return 0;
	}
	size_t heap0 = largest_free();

	printk("heap: %u KiB free at the start\n", (unsigned int)(heap0 >> 10));
	/* the card needs a moment after the power up */
	int mounted = -1;

	for (int i = 0; i < 10 && mounted != 0; i++) {
		mounted = fs_mount(&mp);
		if (mounted != 0) {
			k_msleep(300);
		}
	}
	if (mounted == 0 && pick_file(path, sizeof(path))) {
		html = read_file(path);
		if (html != NULL) {
			source = path;
		}
	} else {
		printk("no SD card\n");
	}
	printk("page: %s\n", source);

	t0 = k_uptime_get();
	page = lh_load(&font, html != NULL ? html : builtin_page, W, H);
	if (page == NULL) {
		printk("lh_load failed\n");
		return 0;
	}
	height = lh_content_height(page);
	printk("page: \"%s\", %d px high, parsed and laid out in %d ms\n", lh_title(page), height,
	       (int)(k_uptime_get() - t0));
	free(html);
	printk("heap: page and library use %u KiB (%u KiB free)\n",
	       (unsigned int)((heap0 - largest_free()) >> 10), (unsigned int)(largest_free() >> 10));

	for (int i = 0; i < 2; i++) {
		fbs[i] = aligned_alloc(64, W * H * sizeof(uint16_t));
		if (fbs[i] == NULL) {
			printk("no memory for the frames\n");
			return 0;
		}
	}

	printk("heap: with the two frames %u KiB used, %u KiB free\n",
	       (unsigned int)((heap0 - largest_free()) >> 10), (unsigned int)(largest_free() >> 10));

	while (true) {
		uint16_t *fb = fbs[cur];
		struct display_sunxi_rgb img = {
			.data = fb, .width = W, .height = H, .stride = W * 2,
		};
		int max_scroll = MAX(0, height - H);
		int ret;

		t0 = k_uptime_get();
		lh_draw(page, fb, W, W, H, scroll, BG);
		sys_cache_data_flush_range(fb, W * H * sizeof(uint16_t));
		if (scroll == 0 || scroll >= max_scroll) {
			printk("page: drawn in %d ms\n", (int)(k_uptime_get() - t0));
		}
		/* returns when the new frame is on the screen: the other one is free again */
		ret = display_sunxi_show_rgb(disp, &img);
		if (ret != 0) {
			printk("show_rgb failed: %d\n", ret);
			return 0;
		}
		cur ^= 1;
		if (max_scroll == 0 || CONFIG_SAMPLE_HTML_SCROLL_PX == 0) {
			k_sleep(K_SECONDS(1));
			continue;
		}
		scroll += dir * CONFIG_SAMPLE_HTML_SCROLL_PX;
		if (scroll >= max_scroll) {
			scroll = max_scroll;
			dir = -1;
			k_sleep(K_SECONDS(2));
		} else if (scroll <= 0) {
			scroll = 0;
			dir = 1;
			k_sleep(K_SECONDS(2));
		}
	}
}
