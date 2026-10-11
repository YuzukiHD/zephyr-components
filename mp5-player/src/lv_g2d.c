/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * A draw unit of LVGL that fills large plain rectangles with the 2D accelerator. Everything else,
 * and every rectangle below the size where the accelerator is faster than the processor, stays
 * with the software renderer. The fill is the opaque colour of a rectangle without radius,
 * gradient, border or blend mode, so the result equals the software fill.
 */

#include <lvgl.h>
#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/g2d.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "draw/lv_draw_private.h"
#include "misc/lv_area_private.h"
#include "lv_g2d.h"

#define G2D_UNIT_ID 7

static const struct device *const g2d = DEVICE_DT_GET(DT_NODELABEL(g2d));
static uint32_t fills, fill_px;

static int32_t g2d_evaluate(lv_draw_unit_t *u, lv_draw_task_t *t)
{
	const lv_draw_fill_dsc_t *dsc = t->draw_dsc;
	lv_area_t a;
	int32_t px;

	ARG_UNUSED(u);
	if (t->type != LV_DRAW_TASK_TYPE_FILL || dsc->radius != 0 || dsc->opa < LV_OPA_MAX ||
	    dsc->grad.dir != LV_GRAD_DIR_NONE ||
	    ((lv_draw_dsc_base_t *)dsc)->layer->color_format != LV_COLOR_FORMAT_ARGB8888) {
		return 0;
	}
	if (!lv_area_intersect(&a, &t->area, &t->clip_area)) {
		return 0;
	}
	px = lv_area_get_width(&a) * lv_area_get_height(&a);
	if (px < CONFIG_SAMPLE_MP5_G2D_MIN_PIXELS) {
		return 0;
	}
	t->preference_score = 0;
	t->preferred_draw_unit_id = G2D_UNIT_ID;

	return 1;
}

/* write back and drop the cache lines of the rows of an area (the neighbours on the same line are kept) */
static void cache_rows(const lv_draw_buf_t *buf, const lv_area_t *a)
{
	for (int32_t y = a->y1; y <= a->y2; y++) {
		sys_cache_data_flush_and_invd_range(
			(uint8_t *)buf->data + (size_t)y * buf->header.stride + (size_t)a->x1 * 4,
			(size_t)lv_area_get_width(a) * 4);
	}
}

static int32_t g2d_dispatch(lv_draw_unit_t *u, lv_layer_t *layer)
{
	lv_draw_task_t *t = lv_draw_get_available_task(layer, NULL, G2D_UNIT_ID);
	const lv_draw_fill_dsc_t *dsc;
	lv_draw_buf_t *buf;
	lv_area_t a;
	struct g2d_surface dst;
	struct g2d_rect r;
	lv_color32_t c;

	ARG_UNUSED(u);
	if (t == NULL || t->preferred_draw_unit_id != G2D_UNIT_ID) {
		return LV_DRAW_UNIT_IDLE;
	}
	if (lv_draw_layer_alloc_buf(layer) == NULL) {
		return LV_DRAW_UNIT_IDLE;
	}
	t->state = LV_DRAW_TASK_STATE_IN_PROGRESS;
	dsc = t->draw_dsc;
	buf = layer->draw_buf;

	if (lv_area_intersect(&a, &t->area, &t->clip_area)) {
		lv_area_move(&a, -layer->buf_area.x1, -layer->buf_area.y1);
		dst = (struct g2d_surface){
			.format = G2D_PIXFMT_ARGB8888,
			.width = buf->header.stride / 4,
			.height = buf->header.h,
			.plane = {buf->data},
			.pitch = {buf->header.stride},
		};
		r = (struct g2d_rect){a.x1, a.y1, lv_area_get_width(&a), lv_area_get_height(&a)};
		c = lv_color_to_32(dsc->color, 0xff);
		/* the processor wrote the pixels around it: the cache lines of the area go out
		 * first, and come back from memory after the accelerator wrote it
		 */
		cache_rows(buf, &a);
		g2d_fill(g2d, &dst, &r, (uint32_t)c.alpha << 24 | (uint32_t)c.red << 16 |
				       (uint32_t)c.green << 8 | c.blue);
		cache_rows(buf, &a);
		fills++;
		fill_px += r.width * r.height;
	}
	t->state = LV_DRAW_TASK_STATE_READY;
	lv_draw_dispatch_request();

	return 1;
}

void lv_g2d_init(void)
{
	lv_draw_unit_t *u;

	if (!device_is_ready(g2d)) {
		printk("lv_g2d: the accelerator is not ready\n");
		return;
	}
	u = lv_draw_create_unit(sizeof(*u));
	u->evaluate_cb = g2d_evaluate;
	u->dispatch_cb = g2d_dispatch;
	u->name = "G2D";
}

void lv_g2d_report(void)
{
	printk("lv_g2d: %u fills, %u pixels\n", fills, fill_px);
	fills = fill_px = 0;
}
