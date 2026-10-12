/*
 * Copyright (c) 2026 Yuzuki Tsuru
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Vector versions of the ARGB8888 blends of the LVGL software renderer, pixel for pixel the same
 * as lv_color_32_32_mix() of lv_draw_sw_blend_to_argb8888.c:
 *
 *   - the foreground has alpha a (a colour with an opacity, or a pixel with its alpha scaled by
 *     the opacity and the mask)
 *   - a >= 253 or the background alpha <= 2: the result is the foreground, alpha a
 *   - a <= 2: the result is the background
 *   - background alpha 255: the colours are mixed with weight a, the result alpha stays 255
 *   - otherwise the result alpha is 255 - ((255 - a) * (255 - bg alpha) >> 8) and the colours
 *     are mixed with the weight a * 255 / result alpha; a weight of 253 or more takes the
 *     foreground, 2 or less the background
 *   - a mix of one channel is ((fg * w + bg * (255 - w)) * 0x8081) >> 23
 *
 * lv_rvv_selftest() runs LVGL's own code and these functions on the same random data.
 */

#include "core/app.h"
#include <lvgl.h>
#include <riscv_vector.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include "draw/sw/blend/lv_draw_sw_blend_private.h"
#include "draw/sw/blend/lv_draw_sw_blend_to_argb8888.h"

#include "lv_rvv.h"

/* the vector intrinsics of this toolchain take unsigned long pointers for 32 bit elements */
#define VLD32(p, vl) __riscv_vle32_v_u32m2((const unsigned long *)(p), (vl))
#define VST32(p, v, vl) __riscv_vse32_v_u32m2((unsigned long *)(p), (v), (vl))

static bool vector_on = true;

void lv_rvv_enable(bool on)
{
	vector_on = on;
}

/* ---- the mix of eight or more pixels ------------------------------------------------------ */

/* one colour channel: ((fg * w + bg * (255 - w)) * 0x8081) >> 23, channels already in bits 0..7 */
static inline vuint32m2_t mix_channel(vuint32m2_t fg, vuint32m2_t bg, vuint32m2_t w, vuint32m2_t iw,
				      size_t vl)
{
	vuint32m2_t t = __riscv_vadd_vv_u32m2(__riscv_vmul_vv_u32m2(fg, w, vl),
					      __riscv_vmul_vv_u32m2(bg, iw, vl), vl);

	return __riscv_vsrl_vx_u32m2(__riscv_vmul_vx_u32m2(t, 0x8081U, vl), 23, vl);
}

/*
 * fg: colour in bits 0..23 (the alpha byte is not used), a: its alpha 0..255 per lane,
 * bg: the destination pixels
 */
static inline vuint32m2_t mix(vuint32m2_t fg, vuint32m2_t a, vuint32m2_t bg, size_t vl)
{
	vuint32m2_t bga = __riscv_vsrl_vx_u32m2(bg, 24, vl);
	vbool16_t m_fg = __riscv_vmor_mm_b16(__riscv_vmsgtu_vx_u32m2_b16(a, 252, vl),
					     __riscv_vmsleu_vx_u32m2_b16(bga, 2, vl), vl);
	vbool16_t m_tr = __riscv_vmandn_mm_b16(__riscv_vmsleu_vx_u32m2_b16(a, 2, vl), m_fg, vl);
	vbool16_t m_opq = __riscv_vmseq_vx_u32m2_b16(bga, 255, vl);
	vbool16_t m_done = __riscv_vmor_mm_b16(m_fg, m_tr, vl);
	vuint32m2_t fgpx = __riscv_vor_vv_u32m2(__riscv_vand_vx_u32m2(fg, 0xffffff, vl),
						__riscv_vsll_vx_u32m2(a, 24, vl), vl);

	/* the common cases need none of the arithmetic below: every lane is opaque, or transparent */
	if ((size_t)__riscv_vcpop_m_b16(m_fg, vl) == vl) {
		return fgpx;
	}
	if ((size_t)__riscv_vcpop_m_b16(m_done, vl) == vl) {
		return __riscv_vmerge_vvm_u32m2(bg, fgpx, m_fg, vl);
	}

	/* result alpha and weight of the lanes whose background is not opaque */
	vuint32m2_t ia = __riscv_vrsub_vx_u32m2(a, 255, vl);
	vuint32m2_t ibga = __riscv_vrsub_vx_u32m2(bga, 255, vl);
	vuint32m2_t ra = __riscv_vrsub_vx_u32m2(
		__riscv_vsrl_vx_u32m2(__riscv_vmul_vv_u32m2(ia, ibga, vl), 8, vl), 255, vl);
	vuint32m2_t w = a;
	vuint32m2_t alpha = __riscv_vmv_v_x_u32m2(255, vl);

	/* a lane that is neither done nor over an opaque background needs the division */
	if ((size_t)__riscv_vcpop_m_b16(__riscv_vmandn_mm_b16(__riscv_vmnot_m_b16(m_done, vl), m_opq, vl),
					vl) != 0) {
		vuint32m2_t den = __riscv_vmaxu_vx_u32m2(ra, 1, vl);
		vuint32m2_t ratio = __riscv_vdivu_vv_u32m2(__riscv_vmul_vx_u32m2(a, 255, vl), den, vl);

		w = __riscv_vmerge_vvm_u32m2(ratio, a, m_opq, vl);
		alpha = __riscv_vmerge_vxm_u32m2(ra, 255, m_opq, vl);
	}
	vuint32m2_t iw = __riscv_vrsub_vx_u32m2(w, 255, vl);

	vuint32m2_t r = mix_channel(__riscv_vand_vx_u32m2(__riscv_vsrl_vx_u32m2(fg, 16, vl), 0xff, vl),
				    __riscv_vand_vx_u32m2(__riscv_vsrl_vx_u32m2(bg, 16, vl), 0xff, vl),
				    w, iw, vl);
	vuint32m2_t g = mix_channel(__riscv_vand_vx_u32m2(__riscv_vsrl_vx_u32m2(fg, 8, vl), 0xff, vl),
				    __riscv_vand_vx_u32m2(__riscv_vsrl_vx_u32m2(bg, 8, vl), 0xff, vl),
				    w, iw, vl);
	vuint32m2_t b = mix_channel(__riscv_vand_vx_u32m2(fg, 0xff, vl),
				    __riscv_vand_vx_u32m2(bg, 0xff, vl), w, iw, vl);
	vuint32m2_t mixed = __riscv_vor_vv_u32m2(
		__riscv_vor_vv_u32m2(__riscv_vsll_vx_u32m2(r, 16, vl), __riscv_vsll_vx_u32m2(g, 8, vl), vl),
		b, vl);

	/* a weight of 253 or more takes the foreground, 2 or less the background */
	vuint32m2_t fgrgb = __riscv_vand_vx_u32m2(fg, 0xffffff, vl);
	vuint32m2_t bgrgb = __riscv_vand_vx_u32m2(bg, 0xffffff, vl);

	mixed = __riscv_vmerge_vvm_u32m2(mixed, fgrgb, __riscv_vmsgtu_vx_u32m2_b16(w, 252, vl), vl);
	mixed = __riscv_vmerge_vvm_u32m2(mixed, bgrgb, __riscv_vmsleu_vx_u32m2_b16(w, 2, vl), vl);

	vuint32m2_t res = __riscv_vor_vv_u32m2(mixed, __riscv_vsll_vx_u32m2(alpha, 24, vl), vl);

	res = __riscv_vmerge_vvm_u32m2(res, bg, m_tr, vl);
	res = __riscv_vmerge_vvm_u32m2(res, fgpx, m_fg, vl);

	return res;
}

/* ---- the kernels --------------------------------------------------------------------------- */

enum { PLAIN, OPA, MASK, MASK_OPA };

static inline vuint32m2_t load_u8_as_u32(const uint8_t *p, size_t vl)
{
	return __riscv_vzext_vf4_u32m2(__riscv_vle8_v_u8mf2(p, vl), vl);
}

static inline __attribute__((always_inline)) int color_blend(lv_draw_sw_blend_fill_dsc_t *dsc, int mode)
{
	const uint32_t rgb = lv_color_to_u32(dsc->color) & 0xffffff;
	const lv_opa_t opa = dsc->opa;
	const uint8_t *mask = dsc->mask_buf;
	uint8_t *dst = dsc->dest_buf;

	if (!vector_on) {
		return LV_RESULT_INVALID;
	}
	for (int32_t y = 0; y < dsc->dest_h; y++) {
		uint32_t *d = (uint32_t *)dst;

		for (int32_t x = 0; x < dsc->dest_w;) {
			size_t vl = __riscv_vsetvl_e32m2(dsc->dest_w - x);
			vuint32m2_t a;

			if (mode == OPA) {
				a = __riscv_vmv_v_x_u32m2(opa, vl);
			} else if (mode == MASK) {
				a = load_u8_as_u32(&mask[x], vl);
			} else {
				a = __riscv_vsrl_vx_u32m2(
					__riscv_vmul_vx_u32m2(load_u8_as_u32(&mask[x], vl), opa, vl), 8, vl);
			}
			VST32(&d[x], mix(__riscv_vmv_v_x_u32m2(rgb, vl), a,
							 VLD32(&d[x], vl), vl), vl);
			x += vl;
		}
		dst += dsc->dest_stride;
		mask += dsc->mask_stride;
	}

	return LV_RESULT_OK;
}

int lv_rvv_color_fill(lv_draw_sw_blend_fill_dsc_t *dsc)
{
	const uint32_t color32 = lv_color_to_u32(dsc->color);
	uint8_t *dst = dsc->dest_buf;

	if (!vector_on) {
		return LV_RESULT_INVALID;
	}
	for (int32_t y = 0; y < dsc->dest_h; y++) {
		uint32_t *d = (uint32_t *)dst;

		for (int32_t x = 0; x < dsc->dest_w;) {
			size_t vl = __riscv_vsetvl_e32m8(dsc->dest_w - x);

			__riscv_vse32_v_u32m8((unsigned long *)&d[x], __riscv_vmv_v_x_u32m8(color32, vl), vl);
			x += vl;
		}
		dst += dsc->dest_stride;
	}

	return LV_RESULT_OK;
}

int lv_rvv_color_opa(lv_draw_sw_blend_fill_dsc_t *dsc)
{
	return color_blend(dsc, OPA);
}

int lv_rvv_color_mask(lv_draw_sw_blend_fill_dsc_t *dsc)
{
	return color_blend(dsc, MASK);
}

int lv_rvv_color_mask_opa(lv_draw_sw_blend_fill_dsc_t *dsc)
{
	return color_blend(dsc, MASK_OPA);
}

static inline __attribute__((always_inline)) int image_blend(lv_draw_sw_blend_image_dsc_t *dsc, int mode)
{
	const lv_opa_t opa = dsc->opa;
	const uint8_t *src = dsc->src_buf;
	const uint8_t *mask = dsc->mask_buf;
	uint8_t *dst = dsc->dest_buf;

	if (!vector_on) {
		return LV_RESULT_INVALID;
	}
	for (int32_t y = 0; y < dsc->dest_h; y++) {
		uint32_t *d = (uint32_t *)dst;
		const uint32_t *s = (const uint32_t *)src;

		for (int32_t x = 0; x < dsc->dest_w;) {
			size_t vl = __riscv_vsetvl_e32m2(dsc->dest_w - x);
			vuint32m2_t px = VLD32(&s[x], vl);
			vuint32m2_t a = __riscv_vsrl_vx_u32m2(px, 24, vl);

			if (mode == OPA) {
				a = __riscv_vsrl_vx_u32m2(__riscv_vmul_vx_u32m2(a, opa, vl), 8, vl);
			} else if (mode == MASK) {
				a = __riscv_vsrl_vx_u32m2(
					__riscv_vmul_vv_u32m2(a, load_u8_as_u32(&mask[x], vl), vl), 8, vl);
			} else if (mode == MASK_OPA) {
				a = __riscv_vsrl_vx_u32m2(
					__riscv_vmul_vv_u32m2(
						__riscv_vmul_vx_u32m2(a, opa, vl),
						load_u8_as_u32(&mask[x], vl), vl), 16, vl);
			}
			VST32(&d[x], mix(px, a, VLD32(&d[x], vl), vl), vl);
			x += vl;
		}
		dst += dsc->dest_stride;
		src += dsc->src_stride;
		mask += dsc->mask_stride;
	}

	return LV_RESULT_OK;
}

int lv_rvv_argb_normal(lv_draw_sw_blend_image_dsc_t *dsc)
{
	return image_blend(dsc, PLAIN);
}

int lv_rvv_argb_opa(lv_draw_sw_blend_image_dsc_t *dsc)
{
	return image_blend(dsc, OPA);
}

int lv_rvv_argb_mask(lv_draw_sw_blend_image_dsc_t *dsc)
{
	return image_blend(dsc, MASK);
}

int lv_rvv_argb_mask_opa(lv_draw_sw_blend_image_dsc_t *dsc)
{
	return image_blend(dsc, MASK_OPA);
}

/* a fixed seed gives the same data on every run */
static uint32_t rng_state = 0x1234567U;

static uint32_t rng(void)
{
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 17;
	rng_state ^= rng_state << 5;

	return rng_state;
}

/* ---- self test ----------------------------------------------------------------------------- */

#define ST_W_MAX 70
#define ST_H 5
#define ST_PAD 3

/* alphas that hit every branch of the mix, and random ones */
static uint32_t pick_alpha(void)
{
	static const uint8_t fixed[] = {0, 1, 2, 3, 4, 100, 128, 200, 251, 252, 253, 254, 255};
	uint32_t r = rng();

	return (r & 1U) ? fixed[(r >> 1) % ARRAY_SIZE(fixed)] : (r >> 8) & 0xffU;
}

static uint32_t pick_pixel(void)
{
	return (rng() & 0xffffffU) | (pick_alpha() << 24);
}

static bool st_one(int kind, int w, int h)
{
	const int stride_px = w + ST_PAD;
	const size_t n = (size_t)stride_px * h;
	uint32_t *dst_a = malloc(n * 4), *dst_b = malloc(n * 4), *src = malloc(n * 4);
	uint8_t *mask = malloc(n);
	bool ok = true;

	if (dst_a == NULL || dst_b == NULL || src == NULL || mask == NULL) {
		free(dst_a);
		free(dst_b);
		free(src);
		free(mask);
		return false;
	}
	for (size_t i = 0; i < n; i++) {
		dst_a[i] = dst_b[i] = pick_pixel();
		src[i] = pick_pixel();
		mask[i] = (rng() & 3U) ? pick_alpha() : 255;
	}
	{
		/* kind: 0..3 colour (plain, opa, mask, mask+opa), 4..7 the same for ARGB8888 pixels */
		bool is_img = kind >= 4;
		int mode = kind & 3;
		lv_opa_t opa = (mode == PLAIN || mode == MASK) ? 255 : (lv_opa_t)pick_alpha();
		lv_draw_sw_blend_fill_dsc_t f = {
			.dest_w = w, .dest_h = h, .dest_stride = stride_px * 4,
			.mask_buf = (mode == MASK || mode == MASK_OPA) ? mask : NULL,
			.mask_stride = stride_px, .color = lv_color_hex(rng() & 0xffffffU),
			.opa = opa,
		};
		lv_draw_sw_blend_image_dsc_t im = {
			.dest_w = w, .dest_h = h, .dest_stride = stride_px * 4, .src_buf = src,
			.src_stride = stride_px * 4, .src_color_format = LV_COLOR_FORMAT_ARGB8888,
			.mask_buf = f.mask_buf, .mask_stride = stride_px, .opa = opa,
			.blend_mode = LV_BLEND_MODE_NORMAL,
		};

		/* LVGL takes the plain/opacity paths by the values of opa and the mask */
		if (mode == PLAIN) {
			f.opa = im.opa = 255;
		} else if (mode == OPA && opa >= LV_OPA_MAX) {
			f.opa = im.opa = 200;
		}
		for (int pass = 0; pass < 2; pass++) {
			uint32_t *d = pass == 0 ? dst_a : dst_b;

			lv_rvv_enable(pass == 1);
			f.dest_buf = im.dest_buf = d;
			if (is_img) {
				lv_draw_sw_blend_image_to_argb8888(&im);
			} else {
				lv_draw_sw_blend_color_to_argb8888(&f);
			}
		}
		lv_rvv_enable(true);
	}
	for (size_t i = 0; i < n && ok; i++) {
		/* the pixels between the rows (the padding) must be untouched, too */
		if (dst_a[i] != dst_b[i]) {
			printk("lv_rvv: kind %d w %d h %d pixel %u: scalar %08x vector %08x\n", kind, w, h,
			       (unsigned)i, (unsigned)dst_a[i], (unsigned)dst_b[i]);
			ok = false;
		}
	}
	free(dst_a);
	free(dst_b);
	free(src);
	free(mask);

	return ok;
}

bool lv_rvv_selftest(void)
{
	unsigned int bad = 0, runs = 0;

	for (int round = 0; round < 40; round++) {
		for (int kind = 0; kind < 8; kind++) {
			int w = 1 + (int)(rng() % ST_W_MAX);

			runs++;
			if (!st_one(kind, w, ST_H)) {
				bad++;
			}
		}
	}
	printk("lv_rvv: %u runs, %u differ\n", runs, bad);

	return bad == 0;
}

static int rvv_step(void)
{
	if (!lv_rvv_selftest()) {
		printk("mp5: the vector blends differ from LVGL, switched off\n");
		lv_rvv_enable(false);
	}

	return 0;
}
MP5_INIT(MP5_STAGE_EARLY, 20, "vector blends", rvv_step);
