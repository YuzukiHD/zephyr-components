/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Scrolling by moving what is on the screen. Drawing a list again for every step of a scroll costs
 * 250 ms (the memory is the limit, not the processor); copying the picture by the step with the 2D
 * accelerator costs 9 ms, and only the rows that come in need drawing.
 *
 * The frame buffer is what the display engine scans out, from the top, all the time. A copy that
 * runs while it is read shows a seam: the part above the line being read is old, the part below new
 * (a double picture, which looks like a trail on a fast scroll). So the copy starts at a vertical
 * blank and goes from the top to the bottom, which it does faster than the display reads, and it
 * never overtakes nor is overtaken. Up is one operation inside the buffer (the lines are read ahead
 * of the ones written). Down cannot be done inside the buffer in that order: the lines go to a second
 * buffer first, and from there to the frame buffer at the blank.
 *
 * The rows that come in are filled with the colour of the page, so that what shows until they are
 * drawn is not a copy of other rows. Pixels with alpha 0 come out of the accelerator as 0, which is
 * the same transparent pixel.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/g2d.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "gfx/shift.h"

#define PAGE_COLOR	0xff0b0f14U

static const struct device *const g2d = DEVICE_DT_GET(DT_NODELABEL(g2d));
static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static uint8_t *fb, *bounce;
static size_t bounce_size;
static uint32_t stride, fb_w, fb_h;
static bool ok;

bool shift_ready(void)
{
	if (!ok) {
		struct display_capabilities caps;

		if (!device_is_ready(g2d) || !device_is_ready(disp)) {
			return false;
		}
		display_get_capabilities(disp, &caps);
		fb = display_sunxi_framebuffer(disp, &stride);
		fb_w = caps.x_resolution;
		fb_h = caps.y_resolution;
		ok = fb != NULL && stride == fb_w * 4;
		if (!ok) {
			printk("shift: the frame buffer is not what the accelerator expects\n");
		}
	}

	return ok;
}

void shift_release(void)
{
	free(bounce);
	bounce = NULL;
	bounce_size = 0;
}

static void surfaces(struct g2d_surface *f, struct g2d_surface *b)
{
	*f = (struct g2d_surface){.format = G2D_PIXFMT_ARGB8888, .width = fb_w, .height = fb_h, .plane = {fb},
				  .pitch = {stride}};
	*b = (struct g2d_surface){.format = G2D_PIXFMT_ARGB8888, .width = fb_w, .height = fb_h, .plane = {bounce},
				  .pitch = {stride}};
}

int shift_rows(int y, int h, int dy)
{
	int n = dy < 0 ? -dy : dy, moved = h - n, first_new, ret;
	struct g2d_surface f, b;
	struct g2d_rect src, dst, strip;
	size_t bytes;

	if (!shift_ready() || n == 0 || moved <= 0 || y < 0 || y + h > (int)fb_h) {
		return -EINVAL;
	}
	bytes = (size_t)moved * stride;
	if (dy < 0) {
		if (bounce_size < bytes) {
			shift_release();
			bounce = aligned_alloc(64, bytes);
			if (bounce == NULL) {
				return -ENOMEM;
			}
			bounce_size = bytes;
			/* only the accelerator writes it from now on: no dirty line of the processor may come later */
			sys_cache_data_flush_and_invd_range(bounce, bytes);
		}
		surfaces(&f, &b);
		/* the picture goes to the second buffer at once; the move at the blank */
		sys_cache_data_flush_range(fb + (size_t)y * stride, bytes);
		src = (struct g2d_rect){0, y, fb_w, moved};
		dst = (struct g2d_rect){0, 0, fb_w, moved};
		ret = g2d_blit(g2d, &f, &src, &b, &dst, G2D_ROTATE_0, G2D_FLAG_NO_CACHE_OPS);
		if (ret != 0) {
			return ret;
		}
		display_sunxi_wait_vsync(disp, 40);
		src = (struct g2d_rect){0, 0, fb_w, moved};
		dst = (struct g2d_rect){0, y + n, fb_w, moved};
		ret = g2d_blit(g2d, &b, &src, &f, &dst, G2D_ROTATE_0, G2D_FLAG_NO_CACHE_OPS);
		first_new = y;
	} else {
		surfaces(&f, &b);
		sys_cache_data_flush_range(fb + (size_t)(y + n) * stride, bytes);
		display_sunxi_wait_vsync(disp, 40);
		src = (struct g2d_rect){0, y + n, fb_w, moved};
		dst = (struct g2d_rect){0, y, fb_w, moved};
		ret = g2d_blit(g2d, &f, &src, &f, &dst, G2D_ROTATE_0, G2D_FLAG_NO_CACHE_OPS);
		first_new = y + moved;
	}
	if (ret == 0) {
		strip = (struct g2d_rect){0, first_new, fb_w, n};
		g2d_fill(g2d, &f, &strip, PAGE_COLOR);
	}
	sys_cache_data_invd_range(fb + (size_t)y * stride, (size_t)h * stride);

	return ret;
}

/*
 * A copy to the left inside one buffer reads and writes the same rows. Whether the accelerator
 * reads far enough ahead of what it writes is checked once, on a small buffer against memmove().
 */
static int cols_state;		/* 0 not checked, 1 right, -1 wrong */

static bool cols_check(void)
{
	enum { W = 128, H = 16, STEP = 7 };
	uint32_t *buf = aligned_alloc(64, W * H * 4), *ref = malloc(W * H * 4);
	bool good = false;

	if (buf != NULL && ref != NULL) {
		struct g2d_surface s = {.format = G2D_PIXFMT_ARGB8888, .width = W, .height = H, .plane = {buf},
					.pitch = {W * 4}};
		struct g2d_rect src = {STEP, 0, W - STEP, H}, dst = {0, 0, W - STEP, H};

		for (int i = 0; i < W * H; i++) {
			buf[i] = ref[i] = 0xff000000U | (uint32_t)i * 2654435761U >> 8;
		}
		for (int y = 0; y < H; y++) {
			memmove(&ref[y * W], &ref[y * W + STEP], (W - STEP) * 4);
		}
		sys_cache_data_flush_and_invd_range(buf, W * H * 4);
		if (g2d_blit(g2d, &s, &src, &s, &dst, G2D_ROTATE_0, G2D_FLAG_NO_CACHE_OPS) == 0) {
			sys_cache_data_invd_range(buf, W * H * 4);
			good = true;
			for (int y = 0; y < H && good; y++) {
				good = memcmp(&buf[y * W], &ref[y * W], (W - STEP) * 4) == 0;
			}
		}
	}
	free(buf);
	free(ref);
	printk("shift: a copy to the left inside the buffer is %s\n", good ? "right" : "WRONG");

	return good;
}

static uint32_t cols_n, cols_wait_us, cols_total_us;

void shift_cols_times(uint32_t *n, uint32_t *wait_us, uint32_t *total_us)
{
	*n = cols_n;
	*wait_us = cols_wait_us;
	*total_us = cols_total_us;
	cols_n = cols_wait_us = cols_total_us = 0;
}

int shift_cols(int x, int y, int w, int h, int dx, bool sync)
{
	uint32_t t0 = k_cycle_get_32(), t1;

	struct g2d_surface f, b;
	struct g2d_rect src, dst, strip;
	int ret;

	if (!shift_ready() || dx <= 0 || dx >= w || x < 0 || y < 0 || x + w > (int)fb_w || y + h > (int)fb_h) {
		return -EINVAL;
	}
	if (cols_state == 0) {
		cols_state = cols_check() ? 1 : -1;
	}
	if (cols_state < 0) {
		return -ENOTSUP;
	}
	surfaces(&f, &b);
	sys_cache_data_flush_range(fb + (size_t)y * stride, (size_t)h * stride);
	if (sync) {
		t1 = k_cycle_get_32();
		display_sunxi_wait_vsync(disp, 40);
		cols_wait_us += k_cyc_to_us_floor32(k_cycle_get_32() - t1);
	}
	src = (struct g2d_rect){x + dx, y, w - dx, h};
	dst = (struct g2d_rect){x, y, w - dx, h};
	ret = g2d_blit(g2d, &f, &src, &f, &dst, G2D_ROTATE_0, G2D_FLAG_NO_CACHE_OPS);
	if (ret == 0) {
		strip = (struct g2d_rect){x + w - dx, y, dx, h};
		g2d_fill(g2d, &f, &strip, 0);
	}
	sys_cache_data_invd_range(fb + (size_t)y * stride, (size_t)h * stride);
	cols_n++;
	cols_total_us += k_cyc_to_us_floor32(k_cycle_get_32() - t0);

	return ret;
}
