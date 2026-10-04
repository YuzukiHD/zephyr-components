/*
 * Copyright (c) 2026 Yuzuki Tsuru
 * SPDX-License-Identifier: MPL-2.0
 */

/*
 * Vector versions of the inner loop of the background renderer: one row of a
 * tile, eight pixels, drawn into the scanline buffer. They do what
 * BACKGROUND_DRAW_PIXEL_16/256 do for the case without blending and without
 * the object window, pixel for pixel:
 *
 *   a pixel is written when its tile data is not 0 and the pixel in the line
 *   buffer may still be written; the colour comes from the plain palette, or
 *   from the normal one when the buffer holds a pixel that must be blended
 *   again; it replaces the buffer pixel when its priority bits are lower.
 */

#ifndef SOFTWARE_RVV_H_
#define SOFTWARE_RVV_H_

#include "gba/renderers/software-private.h"

#if defined(__riscv_v) && !defined(MGBA_NO_RVV)
#include <riscv_vector.h>

#define RVV_KEEP_MASK (0x00FFFFFFu | FLAG_REBLEND | FLAG_OBJWIN)

/* mColorMix5Bit() for eight pixels, 5-6-5 colours: green sits above the red and blue fields while mixing */
static inline vuint32m2_t rvv_mix565(vuint32m2_t colorA, int weightA, vuint32m2_t colorB, int weightB, size_t vl) {
	vuint32m2_t a = __riscv_vor_vv_u32m2(__riscv_vand_vx_u32m2(colorA, 0xF81F, vl),
		__riscv_vsll_vx_u32m2(__riscv_vand_vx_u32m2(colorA, 0x7C0, vl), 16, vl), vl);
	vuint32m2_t b = __riscv_vor_vv_u32m2(__riscv_vand_vx_u32m2(colorB, 0xF81F, vl),
		__riscv_vsll_vx_u32m2(__riscv_vand_vx_u32m2(colorB, 0x7C0, vl), 16, vl), vl);
	vuint32m2_t c = __riscv_vsrl_vx_u32m2(__riscv_vadd_vv_u32m2(
		__riscv_vmul_vx_u32m2(a, weightA, vl), __riscv_vmul_vx_u32m2(b, weightB, vl), vl), 4, vl);
	vbool16_t m;

	m = __riscv_vmsne_vx_u32m2_b16(__riscv_vand_vx_u32m2(c, 0x08000000u, vl), 0, vl);
	c = __riscv_vmerge_vvm_u32m2(c, __riscv_vor_vx_u32m2(__riscv_vand_vx_u32m2(c, ~0x0FC00000u, vl), 0x07C00000u, vl), m, vl);
	m = __riscv_vmsne_vx_u32m2_b16(__riscv_vand_vx_u32m2(c, 0x0020u, vl), 0, vl);
	c = __riscv_vmerge_vvm_u32m2(c, __riscv_vor_vx_u32m2(__riscv_vand_vx_u32m2(c, ~0x003Fu, vl), 0x001Fu, vl), m, vl);
	m = __riscv_vmsne_vx_u32m2_b16(__riscv_vand_vx_u32m2(c, 0x10000u, vl), 0, vl);
	c = __riscv_vmerge_vvm_u32m2(c, __riscv_vor_vx_u32m2(__riscv_vand_vx_u32m2(c, ~0x1F800u, vl), 0xF800u, vl), m, vl);

	return __riscv_vor_vv_u32m2(__riscv_vand_vx_u32m2(c, 0xF81F, vl),
		__riscv_vand_vx_u32m2(__riscv_vsrl_vx_u32m2(c, 16, vl), 0x07C0, vl), vl);
}

/*
 * @p idx holds the palette index of each of the 8 pixels. With @p alpha the
 * pixel that wins is mixed with the one below when the two are blend targets
 * (_compositeBlendNoObjwin), otherwise it replaces it (_compositeNoBlendNoObjwin).
 */
static inline void rvv_blend8(uint32_t* pixel, vuint32m2_t cur, vuint32m2_t idx, const mColor* palette,
                              const mColor* normalPalette, uint32_t flags, vbool16_t write, size_t vl,
                              bool alpha, int blda, int bldb) {
	vbool16_t reblend = __riscv_vmseq_vx_u32m2_b16(
		__riscv_vand_vx_u32m2(cur, FLAG_IS_BACKGROUND | FLAG_REBLEND, vl), FLAG_REBLEND, vl);
	vuint32m2_t off = __riscv_vsll_vx_u32m2(idx, 1, vl);
	off = __riscv_vadd_vx_u32m2_mu(reblend, off, off, (uint32_t) ((const char*) normalPalette - (const char*) palette), vl);
	vuint32m2_t color = __riscv_vor_vx_u32m2(
		__riscv_vzext_vf2_u32m2(__riscv_vluxei32_v_u16m1((const uint16_t*) palette, off, vl), vl), flags, vl);
	/* color >= current: the pixel below stays on top */
	vbool16_t below = __riscv_vmsleu_vv_u32m2_b16(cur, color, vl);
	vuint32m2_t result = __riscv_vmerge_vvm_u32m2(__riscv_vand_vx_u32m2(color, alpha ? ~FLAG_TARGET_2 : ~0u, vl),
		__riscv_vand_vx_u32m2(cur, RVV_KEEP_MASK, vl), below, vl);

	if (alpha) {
		vbool16_t mix = __riscv_vmand_mm_b16(below, __riscv_vmand_mm_b16(
			__riscv_vmsne_vx_u32m2_b16(__riscv_vand_vx_u32m2(cur, FLAG_TARGET_1, vl), 0, vl),
			__riscv_vmsne_vx_u32m2_b16(__riscv_vand_vx_u32m2(color, FLAG_TARGET_2, vl), 0, vl), vl), vl);

		result = __riscv_vmerge_vvm_u32m2(result, rvv_mix565(cur, blda, color, bldb, vl), mix, vl);
	} else {
		/* without blending the pixel that loses is just the pixel below */
		result = __riscv_vmerge_vvm_u32m2(color, __riscv_vand_vx_u32m2(cur, RVV_KEEP_MASK, vl), below, vl);
	}
	__riscv_vse32_v_u32m2_m(write, (unsigned long*) pixel, result, vl);
}

static inline vbool16_t rvv_writable(vuint32m2_t cur, vuint32m2_t idx, size_t vl) {
	return __riscv_vmand_mm_b16(__riscv_vmsne_vx_u32m2_b16(idx, 0, vl),
	                            __riscv_vmsne_vx_u32m2_b16(__riscv_vand_vx_u32m2(cur, 0xFE000000u, vl), 0, vl), vl);
}

/* 16 colours: eight nibbles of tileData, lowest first */
static inline void rvv_draw8_16(uint32_t* pixel, uint32_t tileData, unsigned paletteData, const mColor* palette,
                                const mColor* normalPalette, uint32_t flags, bool alpha, int blda, int bldb) {
	size_t vl = __riscv_vsetvl_e32m2(8);
	vuint32m2_t cur = __riscv_vle32_v_u32m2((const unsigned long*) pixel, vl);
	vuint32m2_t shift = __riscv_vsll_vx_u32m2(__riscv_vid_v_u32m2(vl), 2, vl);
	vuint32m2_t nib = __riscv_vand_vx_u32m2(__riscv_vsrl_vv_u32m2(__riscv_vmv_v_x_u32m2(tileData, vl), shift, vl), 0xF, vl);
	vbool16_t write = rvv_writable(cur, nib, vl);

	rvv_blend8(pixel, cur, __riscv_vor_vx_u32m2(nib, paletteData, vl), palette, normalPalette, flags, write, vl,
	           alpha, blda, bldb);
}

/* 256 colours: eight bytes, the palette index of each pixel */
static inline void rvv_draw8_256(uint32_t* pixel, const uint8_t* src, const mColor* palette,
                                 const mColor* normalPalette, uint32_t flags, bool alpha, int blda, int bldb) {
	size_t vl = __riscv_vsetvl_e32m2(8);
	vuint32m2_t cur = __riscv_vle32_v_u32m2((const unsigned long*) pixel, vl);
	vuint32m2_t idx = __riscv_vzext_vf4_u32m2(__riscv_vle8_v_u8mf2(src, vl), vl);
	vbool16_t write = rvv_writable(cur, idx, vl);

	rvv_blend8(pixel, cur, idx, palette, normalPalette, flags, write, vl, alpha, blda, bldb);
}
#endif

#endif
