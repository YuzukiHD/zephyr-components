/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Scrolling by moving what is on the screen. Drawing a list again for every step of a scroll costs
 * 250 ms (the memory is the limit, not the processor); copying the picture up by the step with the
 * 2D accelerator costs 9 ms, and only the rows that come in need drawing.
 *
 * The accelerator reads and writes the lines from the top: a copy to rows above its source is right
 * in one operation, a copy to rows below overwrites what it still has to read. Down is done in pieces
 * as high as the step (nothing overlaps inside one) from the bottom; every operation has a fixed cost
 * of about a millisecond, so a small step down is done by the processor.
 *
 * The frame buffer is what the display engine scans out. Pixels with alpha 0 come out of the
 * accelerator as 0, which is the same transparent pixel.
 */

#include <errno.h>
#include <string.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/display_sunxi.h>
#include <zephyr/drivers/g2d.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "gfx/shift.h"

static const struct device *const g2d = DEVICE_DT_GET(DT_NODELABEL(g2d));
static const struct device *const disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static uint8_t *fb;
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

static int copy(const struct g2d_surface *s, int src_y, int dst_y, int h)
{
	struct g2d_rect src = {0, src_y, fb_w, h}, dst = {0, dst_y, fb_w, h};

	return g2d_blit(g2d, s, &src, s, &dst, G2D_ROTATE_0, G2D_FLAG_NO_CACHE_OPS);
}

int shift_rows(int y, int h, int dy)
{
	int n = dy < 0 ? -dy : dy, moved = h - n;
	struct g2d_surface s;
	int ret = 0;

	if (!shift_ready() || n == 0 || moved <= 0 || y < 0 || y + h > (int)fb_h) {
		return -EINVAL;
	}
	if (dy < 0 && n < 96) {
		/* many small operations cost more than the processor copying */
		memmove(fb + (size_t)(y + n) * stride, fb + (size_t)y * stride, (size_t)moved * stride);
		sys_cache_data_flush_range(fb + (size_t)(y + n) * stride, (size_t)moved * stride);
		return 0;
	}
	/* what the accelerator reads must be in memory, what it writes must be read from memory */
	sys_cache_data_flush_range(fb + (size_t)(dy > 0 ? y + n : y) * stride, (size_t)moved * stride);
	s = (struct g2d_surface){.format = G2D_PIXFMT_ARGB8888, .width = fb_w, .height = fb_h, .plane = {fb},
				 .pitch = {stride}};
	if (dy > 0) {
		ret = copy(&s, y + n, y, moved);
	} else {
		/* the lowest piece first, each piece as high as the step */
		for (int top = moved - n; ret == 0; top -= n) {
			int from = top < 0 ? 0 : top;

			ret = copy(&s, y + from, y + from + n, top < 0 ? n + top : n);
			if (top <= 0) {
				break;
			}
		}
	}
	sys_cache_data_invd_range(fb + (size_t)(dy > 0 ? y : y + n) * stride, (size_t)moved * stride);

	return ret;
}
