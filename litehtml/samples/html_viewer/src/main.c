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
#include "assets.h"
#include "chunked.h"
#ifdef CONFIG_LITEHTML_PAGED_HEAP
#include <zephyr/kernel/mm/demand_paging.h>
#include <litehtml_zephyr/lh_paged.h>
#endif

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

/*
 * The text goes through a buffer outside of the paged region: the file system must not take a
 * page fault while it holds its lock, and a page fault does file system work of its own.
 */
static uint8_t chunk[32768] __aligned(64);

#ifdef CONFIG_LITEHTML_PAGED_HEAP
static bool paged_ok;
#endif

static void paged(bool on)
{
#ifdef CONFIG_LITEHTML_PAGED_HEAP
	if (paged_ok) {
		lh_paged_heap_enable(on);
	}
#else
	ARG_UNUSED(on);
#endif
}

static char *read_file(const char *path)
{
	struct fs_dirent ent;
	struct fs_file_t f;
	char *buf;
	size_t done = 0;

	if (fs_stat(path, &ent) != 0) {
		return NULL;
	}
	paged(true);
	buf = malloc(ent.size + 1);
	paged(false);
	if (buf == NULL) {
		return NULL;
	}
	fs_file_t_init(&f);
	if (fs_open(&f, path, FS_O_READ) != 0) {
		free(buf);
		return NULL;
	}
	while (done < ent.size) {
		ssize_t n = fs_read(&f, chunk, MIN(sizeof(chunk), ent.size - done));

		if (n <= 0) {
			break;
		}
		memcpy(buf + done, chunk, n);
		done += n;
	}
	fs_close(&f);
	buf[done] = '\0';
	return buf;
}

static struct lh_page *load_page(const struct lh_font *font, const char *html)
{
	struct lh_page *page;

	paged(true);
	page = lh_load(font, html, W, H);
	paged(false);
	return page;
}

static void draw_page(struct lh_page *page, uint16_t *fb, int scroll)
{
	paged(true);
	lh_draw(page, fb, W, W, H, scroll, BG);
	paged(false);
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

/* what a page can still use: the paged heap when there is one, else the normal heap */
static size_t avail(void)
{
#ifdef CONFIG_LITEHTML_PAGED_HEAP
	struct lh_paged_stats st;

	if (!paged_ok) {
		return largest_free();
	}
	lh_paged_heap_stats(&st);
	return st.free;
#else
	return largest_free();
#endif
}

static size_t in_use(void)
{
#ifdef CONFIG_LITEHTML_PAGED_HEAP
	struct lh_paged_stats st;

	if (!paged_ok) {
		return 0;
	}
	lh_paged_heap_stats(&st);
	return st.allocated;
#else
	return 0;
#endif
}

static void paging_report(const char *what)
{
#ifdef CONFIG_DEMAND_PAGING_STATS
	struct k_mem_paging_stats_t ps;

	k_mem_paging_stats_get(&ps);
	printk("paging: %s: %lu faults, %lu clean and %lu dirty pages evicted\n", what,
	       ps.pagefaults.cnt, ps.eviction.clean, ps.eviction.dirty);
#else
	ARG_UNUSED(what);
#endif
}

#ifdef CONFIG_SAMPLE_HTML_BENCH
struct bench_file {
	char name[64];
	size_t size;
};

static int bench_cmp(const void *a, const void *b)
{
	const struct bench_file *x = a, *y = b;

	return x->size < y->size ? -1 : x->size > y->size;
}

static void bench_all(const struct lh_font *font)
{
	static struct bench_file files[16];
	struct fs_dirent ent;
	struct fs_dir_t dir;
	int n = 0;
	uint16_t *fb = aligned_alloc(64, W * H * sizeof(uint16_t));
	size_t base_use;
	/* bytes used per KiB of page by the biggest one that worked, in 1/1024 */
	size_t per_kib = 0;

	fs_dir_t_init(&dir);
	if (fb == NULL || fs_opendir(&dir, "/SD:") != 0) {
		return;
	}
	while (n < ARRAY_SIZE(files) && fs_readdir(&dir, &ent) == 0 && ent.name[0] != '\0') {
		if (ent.type == FS_DIR_ENTRY_FILE && strncmp(ent.name, "page_", 5) == 0 &&
		    name_is_html(ent.name)) {
			strncpy(files[n].name, ent.name, sizeof(files[n].name) - 1);
			files[n].size = ent.size;
			n++;
		}
	}
	fs_closedir(&dir);
	qsort(files, n, sizeof(files[0]), bench_cmp);
	base_use = in_use();
	printk("bench: %d pages, %u KiB of heap free\n", n, (unsigned int)(avail() >> 10));

	for (int i = 0; i < n; i++) {
		char path[96];
		size_t size = files[i].size, free_now = avail();
		struct lh_page *page;
		char *html;
		int64_t t0;
		size_t used, before = largest_free();

		snprintk(path, sizeof(path), "/SD:/%s", files[i].name);
		/* the text stays in memory while the page is built */
		if (size + 1 > free_now ||
		    (per_kib != 0 && size + (size / 1024) * per_kib * 12 / 10 > free_now)) {
			printk("bench: %s %u KiB: does not fit (%u KiB free, %u bytes of heap per byte of page)\n",
			       files[i].name, (unsigned int)(size >> 10), (unsigned int)(free_now >> 10),
			       (unsigned int)(per_kib / 1024));
			continue;
		}
		t0 = k_uptime_get();
		html = read_file(path);
		if (html == NULL) {
			printk("bench: %s: read failed\n", files[i].name);
			continue;
		}
		printk("bench: %s %u KiB: read in %d ms\n", files[i].name, (unsigned int)(size >> 10),
		       (int)(k_uptime_get() - t0));
		paging_report("after the read");
		t0 = k_uptime_get();
		page = load_page(font, html);
		if (page == NULL) {
			printk("bench: %s: lh_load failed\n", files[i].name);
			free(html);
			continue;
		}
		printk("bench: %s: parsed and laid out in %d ms, %d px high\n", files[i].name,
		       (int)(k_uptime_get() - t0), lh_content_height(page));
		paging_report("after the parse");
		free(html);
		used = in_use() != 0 ? in_use() - base_use : before - largest_free();
		per_kib = used * 1024 / size;
		printk("bench: %s: heap used %u KiB (%u bytes per byte of page)\n", files[i].name,
		       (unsigned int)(used >> 10), (unsigned int)(per_kib / 1024));
		for (int k = 0; k < 3; k++) {
			static const char *const where[] = {"top", "middle", "top again"};

			t0 = k_uptime_get();
			draw_page(page, fb, k == 1 ? lh_content_height(page) / 2 : 0);
			printk("bench: %s: frame at %s in %d ms\n", files[i].name, where[k],
			       (int)(k_uptime_get() - t0));
		}
		paging_report("after the frames");
#ifdef CONFIG_LITEHTML_PAGED_HEAP
		if (paged_ok) {
			/* the frees would read the whole tree back: drop the heap instead */
			lh_paged_heap_discard_frees(true);
			lh_free(page);
			lh_paged_heap_discard_frees(false);
			lh_paged_heap_reset();
		} else
#endif
		{
			lh_free(page);
		}
		printk("bench: %s: after the free %u KiB are used\n", files[i].name,
		       (unsigned int)((in_use() - base_use) >> 10));
		paging_report("after the free");
	}
	free(fb);
	printk("bench: done\n");
}
#endif

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
	/* the frames are for the display engine: they come from the normal heap */
	for (int i = 0; i < 2; i++) {
		fbs[i] = aligned_alloc(64, W * H * sizeof(uint16_t));
		if (fbs[i] == NULL) {
			printk("no memory for the frames\n");
			return 0;
		}
	}
	/* the card needs a moment after the power up */
	int mounted = -1;

	for (int i = 0; i < 10 && mounted != 0; i++) {
		mounted = fs_mount(&mp);
		if (mounted != 0) {
			k_msleep(300);
		}
	}
#ifdef CONFIG_LITEHTML_PAGED_HEAP
	if (mounted == 0) {
		int r = lh_paged_heap_init("/SD:/swap.bin", (size_t)CONFIG_SAMPLE_HTML_PAGED_MB << 20);

		paged_ok = (r == 0);
		printk("paged heap of %d MiB: %d\n", CONFIG_SAMPLE_HTML_PAGED_MB, r);
	}
#endif
	if (mounted == 0 && pick_file(path, sizeof(path))) {
		assets_init(path);
#if CONFIG_SAMPLE_HTML_CHUNK_KB > 0
		/* returns only when the page cannot be shown piece by piece */
		chunked_run(path, &font, disp, fbs);
		printk("chunked viewer failed, trying the page as a whole\n");
#endif
		html = read_file(path);
		if (html != NULL) {
			source = path;
		}
	} else {
		printk("no SD card\n");
	}
#ifdef CONFIG_SAMPLE_HTML_BENCH
	if (mounted == 0) {
		bench_all(&font);
	}
#endif
	printk("page: %s\n", source);

	t0 = k_uptime_get();
	page = load_page(&font, html != NULL ? html : builtin_page);
	if (page == NULL) {
		printk("lh_load failed\n");
		return 0;
	}
	height = lh_content_height(page);
	printk("page: \"%s\", %d px high, parsed and laid out in %d ms\n", lh_title(page), height,
	       (int)(k_uptime_get() - t0));
	free(html);
	printk("heap: page and library use %u KiB (%u KiB of the normal heap free)\n",
	       (unsigned int)(in_use() != 0 ? in_use() >> 10 : (heap0 - largest_free()) >> 10),
	       (unsigned int)(largest_free() >> 10));
	paging_report("page loaded");
	assets_report();

	while (true) {
		uint16_t *fb = fbs[cur];
		struct display_sunxi_rgb img = {
			.data = fb, .width = W, .height = H, .stride = W * 2,
		};
		int max_scroll = MAX(0, height - H);
		int ret;

		t0 = k_uptime_get();
		draw_page(page, fb, scroll);
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
