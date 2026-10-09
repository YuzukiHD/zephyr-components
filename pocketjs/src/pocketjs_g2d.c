/* SPDX-License-Identifier: Apache-2.0 */

#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/g2d.h>

#include "pocketjs_zephyr/g2d_accel.h"

#if defined(__riscv_vector)
#include <riscv_vector.h>
#endif

struct pocketjs_g2d {
	const struct device *dev;
	pocketjs_g2d_config_t config;
	pocketjs_rgb565_accelerator_t accel;
	uint32_t *stage;
	size_t stage_pixels;
};

static struct g2d_surface strip_surface(uint16_t *destination, size_t pixels, uint32_t width)
{
	return (struct g2d_surface){
		.format = G2D_PIXFMT_RGB565,
		.width = width,
		.height = pixels / width,
		.plane = {destination},
		.pitch = {width * 2U},
	};
}

static bool fits(const pocketjs_rgb565_rect_t *rect, size_t pixels, uint32_t width)
{
	return width != 0U && rect->width != 0U && rect->height != 0U &&
	       rect->x + rect->width <= width && rect->y + rect->height <= pixels / width &&
	       width <= UINT16_MAX && pixels / width <= UINT16_MAX;
}

static bool fill(void *user, uint16_t *destination, size_t pixels, uint32_t width,
		 uint32_t height, pocketjs_rgb565_rect_t rect, uint16_t color)
{
	struct pocketjs_g2d *g = user;
	struct g2d_surface dst = strip_surface(destination, pixels, width);
	struct g2d_rect r = {rect.x, rect.y, rect.width, rect.height};
	uint32_t r8 = (color >> 11) & 31U, g8 = (color >> 5) & 63U, b8 = color & 31U;

	(void)height;
	if (!fits(&rect, pixels, width)) {
		return false;
	}
	/* bit replication: converting back to RGB565 gives the same value */
	r8 = r8 << 3 | r8 >> 2;
	g8 = g8 << 2 | g8 >> 4;
	b8 = b8 << 3 | b8 >> 2;
	return g2d_fill(g->dev, &dst, &r, 0xff000000U | r8 << 16 | g8 << 8 | b8) == 0;
}

/* one row of coverage bytes -> ARGB8888 with a fixed colour */
static void expand_row(uint32_t *out, const uint8_t *mask, size_t n, uint32_t rgb,
		       uint8_t global_alpha)
{
#if defined(__riscv_vector)
	if (global_alpha == 255U) {
		while (n != 0U) {
			size_t vl = __riscv_vsetvl_e8m1(n);
			vuint8m1_t m = __riscv_vle8_v_u8m1(mask, vl);
			vuint32m4_t w = __riscv_vzext_vf4_u32m4(m, vl);

			w = __riscv_vsll_vx_u32m4(w, 24, vl);
			w = __riscv_vor_vx_u32m4(w, (unsigned long)rgb, vl);
			__riscv_vse32_v_u32m4((unsigned long *)out, w, vl);
			mask += vl;
			out += vl;
			n -= vl;
		}
		return;
	}
#endif
	for (size_t i = 0; i < n; i++) {
		uint32_t a = (mask[i] * (uint32_t)global_alpha + 127U) / 255U;

		out[i] = a << 24 | rgb;
	}
}

static bool blend(void *user, uint16_t *destination, size_t pixels, uint32_t width,
		  uint32_t height, const uint8_t *mask, size_t mask_size,
		  pocketjs_rgb565_rect_t rect, uint8_t red, uint8_t green, uint8_t blue,
		  uint8_t alpha)
{
	struct pocketjs_g2d *g = user;
	const size_t area = (size_t)rect.width * rect.height;
	const uint32_t rgb = (uint32_t)red << 16 | (uint32_t)green << 8 | blue;

	(void)height;
	if (!fits(&rect, pixels, width) || mask_size < (size_t)(rect.y + rect.height) * width) {
		return false;
	}
	if (area > g->stage_pixels) {
		uint32_t *p = aligned_alloc(64, ROUND_UP(area * 4U, 64));

		if (p == NULL) {
			return false;
		}
		free(g->stage);
		g->stage = p;
		g->stage_pixels = area;
	}
	for (uint32_t y = 0; y < rect.height; y++) {
		expand_row(g->stage + (size_t)y * rect.width,
			   mask + (size_t)(rect.y + y) * width + rect.x, rect.width, rgb, alpha);
	}

	struct g2d_surface fg = {
		.format = G2D_PIXFMT_ARGB8888,
		.width = rect.width,
		.height = rect.height,
		.plane = {g->stage},
		.pitch = {rect.width * 4U},
	};
	struct g2d_surface dst = strip_surface(destination, pixels, width);
	struct g2d_rect fg_rect = {0, 0, rect.width, rect.height};
	struct g2d_rect r = {rect.x, rect.y, rect.width, rect.height};
	struct g2d_blend b = {
		.mode = G2D_BLEND_SRC_OVER,
		.fg_alpha_mode = G2D_ALPHA_PIXEL,
		.bg_alpha_mode = G2D_ALPHA_GLOBAL,
		.bg_alpha = 255,
	};

	return g2d_blend(g->dev, &fg, &fg_rect, &dst, &r, &dst, &r, &b, 0) == 0;
}

pocketjs_g2d_t *pocketjs_g2d_create(const pocketjs_g2d_config_t *config)
{
	const struct device *dev = DEVICE_DT_GET_ANY(allwinner_sunxi_g2d);
	struct pocketjs_g2d *g;

	if (dev == NULL || !device_is_ready(dev)) {
		return NULL;
	}
	g = calloc(1, sizeof(*g));
	if (g == NULL) {
		return NULL;
	}
	g->dev = dev;
	g->config.min_fill_pixels = config != NULL && config->min_fill_pixels != 0U
					    ? config->min_fill_pixels
					    : CONFIG_POCKETJS_G2D_MIN_FILL;
	g->config.min_blend_pixels = config != NULL && config->min_blend_pixels != 0U
					     ? config->min_blend_pixels
					     : CONFIG_POCKETJS_G2D_MIN_BLEND;
	g->accel = (pocketjs_rgb565_accelerator_t){
		.struct_size = sizeof(g->accel),
		.user_data = g,
		.fill_rgb565 = fill,
		.blend_a8_rgb565 = blend,
	};
	return g;
}

const pocketjs_rgb565_accelerator_t *pocketjs_g2d_accelerator(pocketjs_g2d_t *g)
{
	return g != NULL ? &g->accel : NULL;
}

uint32_t pocketjs_g2d_min_fill(const pocketjs_g2d_t *g)
{
	return g->config.min_fill_pixels;
}

uint32_t pocketjs_g2d_min_blend(const pocketjs_g2d_t *g)
{
	return g->config.min_blend_pixels;
}
