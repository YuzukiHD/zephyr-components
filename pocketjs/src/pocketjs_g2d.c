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
	/* source of the blend of a solid colour: scaled up to the rectangle by the G2D */
	uint32_t *solid;
};

#define SOLID_SIZE 32

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


#if defined(__riscv_vector) && defined(CONFIG_POCKETJS_BLEND_RVV)
/* floor(((s * a + d * (255 - a)) + 127) / 255) for 16 bit lanes, the software blend exactly */
static inline vuint16m2_t mix_channel(vuint16m2_t d, vuint16m2_t a, vuint16m2_t ia, uint16_t s,
				      size_t vl)
{
	vuint16m2_t y = __riscv_vadd_vx_u16m2(
		__riscv_vadd_vv_u16m2(__riscv_vmul_vx_u16m2(a, s, vl),
				      __riscv_vmul_vv_u16m2(d, ia, vl), vl),
		127, vl);

	/* y / 255 == (y + 1 + (y >> 8)) >> 8 for y < 65535 */
	y = __riscv_vadd_vv_u16m2(
		y, __riscv_vadd_vx_u16m2(__riscv_vsrl_vx_u16m2(y, 8, vl), 1, vl), vl);
	return __riscv_vsrl_vx_u16m2(y, 8, vl);
}

/* One row of an A8 mask over RGB565 with one colour; the pixels are those of the software blend */
static void blend_row(uint16_t *dst, const uint8_t *mask, size_t n, uint8_t r, uint8_t g,
		      uint8_t b, uint8_t global_alpha)
{
	while (n != 0U) {
		size_t vl = __riscv_vsetvl_e16m2(n);
		vuint16m2_t a = __riscv_vzext_vf2_u16m2(__riscv_vle8_v_u8m1(mask, vl), vl);

		if (global_alpha != 255U) {
			vuint16m2_t y = __riscv_vadd_vx_u16m2(
				__riscv_vmul_vx_u16m2(a, global_alpha, vl), 127, vl);

			a = __riscv_vsrl_vx_u16m2(
				__riscv_vadd_vv_u16m2(
					y, __riscv_vadd_vx_u16m2(__riscv_vsrl_vx_u16m2(y, 8, vl), 1,
								 vl), vl), 8, vl);
		}

		vuint16m2_t p = __riscv_vle16_v_u16m2(dst, vl);
		vuint16m2_t ia = __riscv_vrsub_vx_u16m2(a, 255, vl);
		vuint16m2_t r5 = __riscv_vsrl_vx_u16m2(p, 11, vl);
		vuint16m2_t g6 = __riscv_vand_vx_u16m2(__riscv_vsrl_vx_u16m2(p, 5, vl), 63, vl);
		vuint16m2_t b5 = __riscv_vand_vx_u16m2(p, 31, vl);
		vuint16m2_t dr = __riscv_vor_vv_u16m2(__riscv_vsll_vx_u16m2(r5, 3, vl),
						      __riscv_vsrl_vx_u16m2(r5, 2, vl), vl);
		vuint16m2_t dg = __riscv_vor_vv_u16m2(__riscv_vsll_vx_u16m2(g6, 2, vl),
						      __riscv_vsrl_vx_u16m2(g6, 4, vl), vl);
		vuint16m2_t db = __riscv_vor_vv_u16m2(__riscv_vsll_vx_u16m2(b5, 3, vl),
						      __riscv_vsrl_vx_u16m2(b5, 2, vl), vl);
		vuint16m2_t mr = mix_channel(dr, a, ia, r, vl);
		vuint16m2_t mg = mix_channel(dg, a, ia, g, vl);
		vuint16m2_t mb = mix_channel(db, a, ia, b, vl);
		vuint16m2_t out = __riscv_vor_vv_u16m2(
			__riscv_vor_vv_u16m2(
				__riscv_vsll_vx_u16m2(__riscv_vand_vx_u16m2(mr, 0xf8, vl), 8, vl),
				__riscv_vsll_vx_u16m2(__riscv_vand_vx_u16m2(mg, 0xfc, vl), 3, vl), vl),
			__riscv_vsrl_vx_u16m2(mb, 3, vl), vl);

		__riscv_vse16_v_u16m2(dst, out, vl);
		dst += vl;
		mask += vl;
		n -= vl;
	}
}
#endif

/*
 * A solid colour with one alpha over a rectangle (the renderer passes an empty mask for it): a
 * small ARGB8888 picture of that colour, scaled to the rectangle, with the alpha as the layer
 * alpha. No mask and no staging buffer of the size of the rectangle.
 */
static bool blend_solid(struct pocketjs_g2d *g, uint16_t *destination, size_t pixels,
			uint32_t width, pocketjs_rgb565_rect_t rect, uint32_t rgb, uint8_t alpha)
{
	if (!fits(&rect, pixels, width) || rect.width > 16U * SOLID_SIZE ||
	    rect.height > 16U * SOLID_SIZE) {
		return false;
	}
	if (g->solid == NULL) {
		g->solid = aligned_alloc(64, SOLID_SIZE * SOLID_SIZE * 4U);
		if (g->solid == NULL) {
			return false;
		}
	}
	for (size_t i = 0; i < SOLID_SIZE * SOLID_SIZE; i++) {
		g->solid[i] = 0xff000000U | rgb;
	}

	struct g2d_surface fg = {
		.format = G2D_PIXFMT_ARGB8888,
		.width = SOLID_SIZE,
		.height = SOLID_SIZE,
		.plane = {g->solid},
		.pitch = {SOLID_SIZE * 4U},
	};
	struct g2d_surface dst = strip_surface(destination, pixels, width);
	struct g2d_rect fg_rect = {0, 0, SOLID_SIZE, SOLID_SIZE};
	struct g2d_rect r = {rect.x, rect.y, rect.width, rect.height};
	struct g2d_blend b = {
		.mode = G2D_BLEND_SRC_OVER,
		.fg_alpha_mode = G2D_ALPHA_GLOBAL,
		.fg_alpha = alpha,
		.bg_alpha_mode = G2D_ALPHA_GLOBAL,
		.bg_alpha = 255,
	};

	return g2d_blend(g->dev, &fg, &fg_rect, &dst, &r, &dst, &r, &b, 0) == 0;
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
	if (mask_size == 0U) {
		return blend_solid(g, destination, pixels, width, rect, rgb, alpha);
	}
	if (!fits(&rect, pixels, width) || mask_size < (size_t)(rect.y + rect.height) * width) {
		return false;
	}
#if defined(__riscv_vector) && defined(CONFIG_POCKETJS_BLEND_RVV)
	/* the memory is the limit here: the mask and the picture are read and written once, no
	 * ARGB copy of the rectangle is made and no cache is cleaned for the G2D */
	for (uint32_t y = 0; y < rect.height; y++) {
		blend_row(destination + (size_t)(rect.y + y) * width + rect.x,
			  mask + (size_t)(rect.y + y) * width + rect.x, rect.width, red, green, blue,
			  alpha);
	}
	return true;
#endif
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
