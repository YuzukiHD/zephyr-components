/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * How fast the 2D accelerator is at the jobs a scrolling list has, against the processor: shifting
 * the picture of the list by 48 lines (a copy inside one buffer, the lines overlap), copying a
 * strip of the draw buffer to the picture, filling, and converting a cover from RGB565. Printed
 * once, a few seconds after the start.
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/g2d.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define W	1024
#define H	700
#define SHIFT	48

static const struct device *const g2d = DEVICE_DT_GET(DT_NODELABEL(g2d));

static uint32_t us_since(uint32_t t0)
{
	return (uint32_t)((uint64_t)(k_cycle_get_32() - t0) * 1000000U / sys_clock_hw_cycles_per_sec());
}

static struct g2d_surface surf(void *p, enum g2d_format f, int w, int h)
{
	return (struct g2d_surface){.format = f, .width = w, .height = h, .plane = {p},
				    .pitch = {(uint32_t)w * g2d_format_bytes_per_pixel(f)}};
}

/* line y of the test picture holds the value y in every word */
static void pattern(uint32_t *fb)
{
	for (int y = 0; y < H; y++) {
		for (int x = 0; x < W; x++) {
			fb[y * W + x] = (uint32_t)y * 0x01010101U + 7;
		}
	}
	sys_cache_data_flush_and_invd_range(fb, W * H * 4);
}

static int wrong_lines(const uint32_t *fb, int first, int last, int from)
{
	int bad = 0, shown = 0;

	sys_cache_data_invd_range((void *)fb, W * H * 4);
	for (int y = first; y < last; y++) {
		if (fb[y * W] != (uint32_t)(y + from) * 0x01010101U + 7 || fb[y * W + W - 1] != fb[y * W]) {
			if (shown++ < 6) {
				printk("g2d bench:   line %d holds %08x\n", y, fb[y * W]);
			}
			bad++;
		}
	}

	return bad;
}

static void bench(void *a, void *b, void *c)
{
	uint32_t *fb = aligned_alloc(64, W * H * 4), *draw = aligned_alloc(64, W * 77 * 4);
	uint16_t *cover = aligned_alloc(64, 512 * 133);
	uint32_t t0, t_g, t_c;
	int rows = H - SHIFT, bad;

	k_sleep(K_SECONDS(8));
	if (fb == NULL || draw == NULL || cover == NULL || !device_is_ready(g2d)) {
		printk("g2d bench: no memory or no accelerator\n");
		return;
	}
	memset(draw, 0x55, W * 77 * 4);
	memset(cover, 0x33, 512 * 133);

	/* the list moves up: the line y + 48 goes to the line y; the lines overlap */
	pattern(fb);
	{
		struct g2d_surface s = surf(fb, G2D_PIXFMT_ARGB8888, W, H);
		struct g2d_rect src = {0, SHIFT, W, rows}, dst = {0, 0, W, rows};

		t0 = k_cycle_get_32();
		g2d_blit(g2d, &s, &src, &s, &dst, G2D_ROTATE_0, 0);
		t_g = us_since(t0);
	}
	bad = wrong_lines(fb, 0, rows, SHIFT);
	printk("g2d bench: shift up %d lines of %d: accelerator %u us (%d lines wrong)\n", rows, W, t_g, bad);

	/* the list moves down: line y goes to line y + 48; the other order of the lines */
	pattern(fb);
	{
		struct g2d_surface s = surf(fb, G2D_PIXFMT_ARGB8888, W, H);
		struct g2d_rect src = {0, 0, W, rows}, dst = {0, SHIFT, W, rows};

		t0 = k_cycle_get_32();
		g2d_blit(g2d, &s, &src, &s, &dst, G2D_ROTATE_0, 0);
		t_g = us_since(t0);
	}
	bad = wrong_lines(fb, SHIFT, H, -SHIFT);
	printk("g2d bench: shift down: accelerator %u us (%d lines wrong)\n", t_g, bad);

	/* chunks of as many lines as the shift: nothing overlaps inside one operation */
	for (int n = 48; n >= 8; n -= 40) {
		int moved = H - n;

		pattern(fb);
		{
			struct g2d_surface s = surf(fb, G2D_PIXFMT_ARGB8888, W, H);
			int ops = 0;

			t0 = k_cycle_get_32();
			/* down: the bottom chunk first */
			for (int y = moved - n; y >= -(n - 1) && moved > 0; y -= n) {
				int top = MAX(y, 0), h = MIN(n, moved - top);
				struct g2d_rect src = {0, top, W, h}, dst = {0, top + n, W, h};

				if (h <= 0) {
					continue;
				}
				g2d_blit(g2d, &s, &src, &s, &dst, G2D_ROTATE_0, G2D_FLAG_NO_CACHE_OPS);
				ops++;
			}
			t_g = us_since(t0);
			printk("g2d bench: shift down by %d in %d operations: %u us\n", n, ops, t_g);
		}
		sys_cache_data_invd_range(fb, W * H * 4);
		bad = wrong_lines(fb, n, H, -n);
		printk("g2d bench:   %d lines wrong\n", bad);
	}

	/* the processor: memmove of the same lines, then the cache lines out (the display reads memory) */
	pattern(fb);
	t0 = k_cycle_get_32();
	memmove(fb, fb + SHIFT * W, (size_t)rows * W * 4);
	sys_cache_data_flush_range(fb, (size_t)rows * W * 4);
	t_c = us_since(t0);
	printk("g2d bench: shift up with memmove and cache flush: %u us\n", t_c);

	/* a strip of the draw buffer to the picture: the flush of LVGL */
	{
		struct g2d_surface s = surf(draw, G2D_PIXFMT_ARGB8888, W, 77), d = surf(fb, G2D_PIXFMT_ARGB8888, W, H);
		struct g2d_rect r = {0, 0, W, 77}, dr = {0, 100, W, 77};

		t0 = k_cycle_get_32();
		g2d_blit(g2d, &s, &r, &d, &dr, G2D_ROTATE_0, 0);
		t_g = us_since(t0);
		t0 = k_cycle_get_32();
		memcpy(fb + 100 * W, draw, W * 77 * 4);
		sys_cache_data_flush_range(fb + 100 * W, W * 77 * 4);
		t_c = us_since(t0);
		printk("g2d bench: draw buffer strip to the picture, 77 lines: accelerator %u us, memcpy and flush %u us\n", t_g, t_c);
	}

	/* a fill of the area of the list */
	{
		struct g2d_surface d = surf(fb, G2D_PIXFMT_ARGB8888, W, H);
		struct g2d_rect r = {0, 0, W, 614};

		t0 = k_cycle_get_32();
		g2d_fill(g2d, &d, &r, 0xff0b0f14);
		t_g = us_since(t0);
		t0 = k_cycle_get_32();
		for (int i = 0; i < W * 614; i++) {
			fb[i] = 0xff0b0f14;
		}
		sys_cache_data_flush_range(fb, W * 614 * 4);
		t_c = us_since(t0);
		printk("g2d bench: fill 1024x614: accelerator %u us, processor %u us\n", t_g, t_c);
	}

	/* a cover from RGB565 to the ARGB8888 picture */
	{
		struct g2d_surface s = surf(cover, G2D_PIXFMT_RGB565, 256, 133), d = surf(fb, G2D_PIXFMT_ARGB8888, W, H);
		struct g2d_rect r = {0, 0, 236, 133}, dr = {16, 200, 236, 133};

		t0 = k_cycle_get_32();
		for (int i = 0; i < 8; i++) {
			g2d_blit(g2d, &s, &r, &d, &dr, G2D_ROTATE_0, 0);
		}
		t_g = us_since(t0);
		printk("g2d bench: 8 covers RGB565 to ARGB8888: accelerator %u us (one: %u us)\n", t_g, t_g / 8);
	}
	free(fb);
	free(draw);
	free(cover);
}

K_THREAD_DEFINE(g2d_bench_thread, 4096, bench, NULL, NULL, NULL, 10, 0, 0);
